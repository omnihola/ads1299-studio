#pragma once

#include <cstdint>
#include <QString>
#include <QByteArray>

#include <libusb.h>

#include "ITransport.h"
#include "MessageDeframer.h"

namespace studio::mmb0 {

/// Real libusb implementation of studio::styx::ITransport for the MMB0.
///
/// Bulk OUT  → EP 0x01 (host→device, packet size 64 B)
/// Bulk IN   → EP 0x81 (device→host, packet size 64 B)
///
/// send() zero-pads the outgoing buffer to a multiple of 64.
/// recv() uses MessageDeframer to reassemble 9P frames that may span
/// several 64-byte USB packets.
class Mmb0UsbTransport : public studio::styx::ITransport {
public:
    Mmb0UsbTransport();
    ~Mmb0UsbTransport() override;

    // Non-copyable, non-movable (owns raw libusb handles).
    Mmb0UsbTransport(const Mmb0UsbTransport&)            = delete;
    Mmb0UsbTransport& operator=(const Mmb0UsbTransport&) = delete;

    /// Returns true if an MMB0 device (VID/PID) is currently enumerated
    /// by the OS.  Safe to call without opening a persistent context.
    static bool isDevicePresent(uint16_t vid = 0x0451, uint16_t pid = 0x5718);

    /// Claim the USB interface and prepare the endpoints for I/O.
    /// Returns true on success; on failure lastError() is set.
    bool open(uint16_t vid = 0x0451, uint16_t pid = 0x5718);

    /// Release the interface and close the device handle.
    void close();

    /// True after a successful open() and before close().
    bool isOpen() const;

    /// Human-readable description of the most recent error.
    QString lastError() const;

    // ── ITransport ──────────────────────────────────────────────────────────

    /// Send one fully-framed 9P T-message over EP 0x01.
    /// The payload is zero-padded to the next multiple of 64 bytes before
    /// transmission.  Returns false on transfer error.
    bool send(const QByteArray& msg) override;

    /// Receive one fully-framed 9P R-message from EP 0x81.
    /// Blocks up to @p timeoutMs milliseconds.  Returns false on timeout
    /// or transfer error.
    bool recv(QByteArray& out, int timeoutMs) override;

private:
    libusb_context*      ctx_  = nullptr;
    libusb_device_handle* dev_ = nullptr;
    studio::styx::MessageDeframer rx_;
    QString              lastError_;

    static constexpr uint8_t kEpOut  = 0x01;
    static constexpr uint8_t kEpIn   = 0x81;
    static constexpr int     kPktSize = 64;

    // Maximum number of 64-byte USB reads per recv() call before giving up.
    static constexpr int kMaxReadsPerRecv = 128;
};

} // namespace studio::mmb0
