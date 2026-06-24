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
    /// Append raw bytes received from the bulk-IN endpoint.
    void feed(const QByteArray& bytes)
    {
        buf_.append(bytes);
    }

    /// If at least one complete 9P message is buffered, move it into
    /// @p msg and return true.  Returns false (and leaves @p msg
    /// unchanged) when no complete message is available yet.
    bool take(QByteArray& msg)
    {
        int len = frameLength(buf_);   // returns -1 if < 4 bytes
        if (len <= 0)
            return false;
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
