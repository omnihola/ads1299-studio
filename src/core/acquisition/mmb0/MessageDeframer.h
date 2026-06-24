#pragma once

#include <QByteArray>
#include "Styx9p.h"

namespace studio::styx {

/// Reassembles size-prefixed 9P frames from a raw byte stream.
///
/// The 9P framing format: the first four bytes of every message carry
/// the total length (including the four-byte size field itself) in
/// little-endian order.  Bytes may arrive in arbitrary chunks; the
/// deframer accumulates them and hands back one complete message at a
/// time via take().
class MessageDeframer {
public:
    /// Upper bound on frame length to detect and discard corrupted/poisoned buffers.
    /// 1 MiB is well above any reasonable 9P msize and serves as a poison detector.
    static constexpr int kMaxFrameLen = 1 << 20;  // 1 MiB ceiling

    /// Append raw bytes received from the bulk-IN endpoint.
    void feed(const QByteArray& bytes)
    {
        buf_.append(bytes);
    }

    /// If at least one complete 9P message is buffered, move it into
    /// @p msg and return true.  Returns false (and leaves @p msg
    /// unchanged) when no complete message is available yet.
    ///
    /// If the frame size field decodes to an absurdly large value (> kMaxFrameLen),
    /// the buffer is cleared (poisoned by corrupt size) and false is returned,
    /// allowing the stream to resync on the next feed().
    bool take(QByteArray& msg)
    {
        int len = frameLength(buf_);   // returns -1 if < 4 bytes
        if (len <= 0)
            return false;
        if (len > kMaxFrameLen) {
            // Poisoned buffer: discard it so the stream can resync.
            clear();
            return false;
        }
        if (buf_.size() < len)
            return false;

        msg = buf_.left(len);
        buf_.remove(0, len);
        return true;
    }

    /// Discard all buffered bytes.
    void clear()
    {
        buf_.clear();
    }

    /// Return the number of bytes currently held in the internal buffer.
    int buffered() const
    {
        return buf_.size();
    }

private:
    QByteArray buf_;
};

} // namespace studio::styx
