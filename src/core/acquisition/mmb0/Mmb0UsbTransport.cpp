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

    // Clear any stalled endpoints from a previous session.
    libusb_clear_halt(dev_, kEpIn);
    libusb_clear_halt(dev_, kEpOut);

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

bool Mmb0UsbTransport::send(const QByteArray& msg)
{
    if (!dev_) {
        lastError_ = QStringLiteral("send called on closed transport");
        return false;
    }

    // Zero-pad to the next multiple of kPktSize.
    int paddedLen = ((msg.size() + kPktSize - 1) / kPktSize) * kPktSize;
    if (paddedLen == 0)
        paddedLen = kPktSize;

    QByteArray buf = msg;
    buf.resize(paddedLen, '\0');

    int transferred = 0;
    int rc = libusb_bulk_transfer(
        dev_,
        kEpOut,
        reinterpret_cast<uint8_t*>(buf.data()),
        paddedLen,
        &transferred,
        1000 /* ms */);

    if (rc != 0) {
        lastError_ = QStringLiteral("bulk OUT failed: %1").arg(libusb_error_name(rc));
        return false;
    }
    return true;
}

bool Mmb0UsbTransport::recv(QByteArray& out, int timeoutMs)
{
    if (!dev_) {
        lastError_ = QStringLiteral("recv called on closed transport");
        return false;
    }

    // Fast path: something is already fully buffered from a previous read.
    if (rx_.take(out))
        return true;

    // Read USB packets until we have a complete 9P frame or we time out /
    // hit the retry limit.
    constexpr int kBufSize = kPktSize * 64; // 4 KiB
    uint8_t rawBuf[kBufSize];

    for (int attempt = 0; attempt < kMaxReadsPerRecv; ++attempt) {
        int got = 0;
        int rc = libusb_bulk_transfer(
            dev_,
            kEpIn,
            rawBuf,
            kBufSize,
            &got,
            timeoutMs);

        if (rc == LIBUSB_ERROR_TIMEOUT) {
            // If we timed out and still don't have a complete frame, give up.
            if (rx_.take(out))
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

        if (rx_.take(out))
            return true;
    }

    // Exhausted retries without assembling a complete frame.
    lastError_ = QStringLiteral("recv: max read attempts reached without a complete frame");
    return false;
}

} // namespace studio::mmb0
