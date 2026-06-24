#pragma once

#include <QByteArray>

namespace studio::styx {

class ITransport {
public:
    virtual ~ITransport() = default;
    // Send one complete 9P message (already size-framed). Return false on error.
    virtual bool send(const QByteArray& msg) = 0;
    // Receive one complete 9P R-message into out (transport handles deframing).
    // Returns false on timeout/error.
    virtual bool recv(QByteArray& out, int timeoutMs) = 0;
};

} // namespace studio::styx
