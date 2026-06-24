#pragma once

#include "ITransport.h"
#include "Styx9p.h"
#include <QByteArray>
#include <QString>
#include <cstdint>

namespace studio::styx {

class Styx9pClient {
public:
    explicit Styx9pClient(ITransport* transport, int timeoutMs = 3000);

    // Tversion handshake; stores negotiated msize.
    bool connectSession(uint32_t msize = 8192,
                        const QString& version = QStringLiteral("9P2000.USB.estyx"));

    // Tattach from root; sets rootFid.
    bool attach(const QString& uname = QString(), const QString& aname = QString());

    uint32_t negotiatedMsize() const;
    QString  lastError() const;

    // High-level named-file ops
    bool writePath(const QString& path, const QByteArray& data);
    bool readPath(const QString& path, QByteArray& out, int maxBytes = 65536);

    // Low-level primitives
    bool walkTo(const QString& path, uint32_t& outFid);
    bool openFid(uint32_t fid, uint8_t mode, uint32_t& iounit);
    bool readFid(uint32_t fid, uint64_t offset, uint32_t count, QByteArray& out);
    bool writeFid(uint32_t fid, uint64_t offset, const QByteArray& data,
                  uint32_t& written);
    bool clunk(uint32_t fid);

private:
    // Send T-message, receive and decode R-message; validate tag and type.
    bool transact(const QByteArray& tmsg, MsgType expectedR, RMessage& out);

    uint32_t allocFid();

    ITransport* m_transport;
    int         m_timeoutMs;
    uint32_t    m_msize    = 8192;
    uint32_t    m_rootFid  = 0;
    uint32_t    m_nextFid  = 1;   // rootFid+1
    uint16_t    m_nextTag  = 1;   // 0xFFFF reserved for Tversion (kNoTag)
    QString     m_lastError;
};

} // namespace studio::styx
