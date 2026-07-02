#include "Mmb0UsbTransport.h"

#include <QByteArray>

namespace studio::mmb0 {

// ── Constructor / destructor ──────────────────────────────────────────────────

Mmb0UsbTransport::Mmb0UsbTransport() = default;

Mmb0UsbTransport::~Mmb0UsbTransport()
{
    close();
}

// ── Static helpers ────────────────────────────────────────────────────────────

bool Mmb0UsbTransport::isDevicePresent(uint16_t vid, uint16_t pid)
{
    libusb_context* ctx = nullptr;
    if (libusb_init(&ctx) != 0)
        return false;

    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &list);
    bool found = false;

    if (count > 0) {
        for (ssize_t i = 0; i < count && !found; ++i) {
            libusb_device_descriptor desc{};
            if (libusb_get_device_descriptor(list[i], &desc) == 0) {
                if (desc.idVendor == vid && desc.idProduct == pid)
                    found = true;
            }
        }
    }

    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return found;
}

// ── open / close / isOpen ─────────────────────────────────────────────────────

bool Mmb0UsbTransport::open(uint16_t vid, uint16_t pid)
{
    // Clean up any previous state.
    close();

    if (libusb_init(&ctx_) != 0) {
        lastError_ = QStringLiteral("libusb_init failed");
        ctx_ = nullptr;
        return false;
    }

    dev_ = libusb_open_device_with_vid_pid(ctx_, vid, pid);
    if (!dev_) {
        lastError_ = QStringLiteral("Device not found (VID=%1 PID=%2)")
                         .arg(vid, 4, 16, QLatin1Char('0'))
                         .arg(pid, 4, 16, QLatin1Char('0'));
        libusb_exit(ctx_);
        ctx_ = nullptr;
        return false;
    }

    // On Linux, detach the kernel driver automatically.
    // The call is silently ignored on macOS / Windows.
    libusb_set_auto_detach_kernel_driver(dev_, 1);

    // Set configuration 1 — ignore EBUSY (already set by OS).
    int rc = libusb_set_configuration(dev_, 1);
    if (rc != 0 && rc != LIBUSB_ERROR_BUSY) {
        lastError_ = QStringLiteral("libusb_set_configuration failed: %1")
                         .arg(libusb_error_name(rc));
        libusb_close(dev_);
        dev_ = nullptr;
        libusb_exit(ctx_);
        ctx_ = nullptr;
        return false;
    }

    rc = libusb_claim_interface(dev_, 0);
    if (rc != 0) {
        lastError_ = QStringLiteral("libusb_claim_interface failed: %1")
                         .arg(libusb_error_name(rc));
        libusb_close(dev_);
        dev_ = nullptr;
        libusb_exit(ctx_);
        ctx_ = nullptr;
        return false;
    }

    // NOTE: deliberately NO libusb_clear_halt() here. The verified reference
    // client never issues CLEAR_FEATURE(HALT); on the USBStyx firmware the
    // control request is suspected of wedging the endpoint state machine
    // (observed on hardware 2026-07-02: with clear_halt in open(), the very
    // first Tattach got no reply — transport recv timeout).

    rx_.clear();
    return true;
}

void Mmb0UsbTransport::close()
{
    if (dev_) {
        libusb_release_interface(dev_, 0);
        libusb_close(dev_);
        dev_ = nullptr;
    }
    if (ctx_) {
        libusb_exit(ctx_);
        ctx_ = nullptr;
    }
    rx_.clear();
}

bool Mmb0UsbTransport::isOpen() const
{
    return dev_ != nullptr;
}

QString Mmb0UsbTransport::lastError() const
{
    return lastError_;
}

// ── ITransport ────────────────────────────────────────────────────────────────

bool Mmb0UsbTransport::needsZlp(int len)
{
    return len > 0 && (len % kPktSize) == 0;
}

bool Mmb0UsbTransport::send(const QByteArray& msg)
{
    if (!dev_) {
        lastError_ = QStringLiteral("send called on closed transport");
        return false;
    }

    // Send exactly the message bytes. Padding to a packet multiple would feed
    // the estyx deframer garbage zeros after the message (it deframes on the
    // size[4] prefix, so trailing zeros decode as a bogus next-message size).
    QByteArray buf = msg;
    int transferred = 0;
    int rc = libusb_bulk_transfer(
        dev_,
        kEpOut,
        reinterpret_cast<uint8_t*>(buf.data()),
        buf.size(),
        &transferred,
        1000 /* ms */);

    if (rc != 0 || transferred != buf.size()) {
        lastError_ = QStringLiteral("bulk OUT failed: %1 (%2/%3 bytes)")
                         .arg(rc != 0 ? libusb_error_name(rc) : "short write")
                         .arg(transferred)
                         .arg(buf.size());
        return false;
    }

    // A transfer that is an exact multiple of the packet size never produces
    // the short packet that marks end-of-transfer — terminate it explicitly
    // with a zero-length packet (verified reference client does the same).
    if (needsZlp(buf.size())) {
        int zero = 0;
        rc = libusb_bulk_transfer(dev_, kEpOut, reinterpret_cast<uint8_t*>(buf.data()),
                                  0, &zero, 1000 /* ms */);
        if (rc != 0) {
            lastError_ = QStringLiteral("bulk OUT ZLP failed: %1").arg(libusb_error_name(rc));
            return false;
        }
    }
    return true;
}

bool Mmb0UsbTransport::recv(QByteArray& out, int timeoutMs)
{
    if (!dev_) {
        lastError_ = QStringLiteral("recv called on closed transport");
        return false;
    }

    // Styx over USBStyx is strict request-reply and the firmware pads its
    // replies to whole 64-byte packets (the official cstyx USBConnection pads
    // both directions to packet multiples). Any bytes left after a complete
    // R-message are padding, NOT the start of the next message — retaining
    // them would seed the deframer with a phantom frame length and every
    // later reply would be swallowed as its continuation (observed on
    // hardware 2026-07-02: attach succeeded, every following call timed out).
    auto takeAndDiscardPadding = [this](QByteArray& msg) {
        if (!rx_.take(msg))
            return false;
        rx_.clear();
        return true;
    };

    // Fast path: something is already fully buffered from a previous read.
    if (takeAndDiscardPadding(out))
        return true;

    // Read USB packets until we have a complete 9P frame or we time out /
    // hit the retry limit. One 64-byte packet per transfer, exactly like the
    // verified reference client (mmb0_acquire.c styx_rx) — larger IN requests
    // against the USBStyx firmware timed out on hardware (2026-07-02).
    uint8_t rawBuf[kPktSize];

    for (int attempt = 0; attempt < kMaxReadsPerRecv; ++attempt) {
        int got = 0;
        int rc = libusb_bulk_transfer(
            dev_,
            kEpIn,
            rawBuf,
            kPktSize,
            &got,
            timeoutMs);

        if (rc == LIBUSB_ERROR_TIMEOUT) {
            // If we timed out and still don't have a complete frame, give up.
            if (takeAndDiscardPadding(out))
                return true;
            lastError_ = QStringLiteral("recv timed out");
            return false;
        }

        if (rc != 0) {
            lastError_ = QStringLiteral("bulk IN failed: %1").arg(libusb_error_name(rc));
            return false;
        }

        if (got > 0)
            rx_.feed(QByteArray(reinterpret_cast<const char*>(rawBuf), got));

        if (takeAndDiscardPadding(out))
            return true;
    }

    // Exhausted retries without assembling a complete frame.
    lastError_ = QStringLiteral("recv: max read attempts reached without a complete frame");
    return false;
}

} // namespace studio::mmb0
