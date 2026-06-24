#include "Styx9pClient.h"

#include <QDebug>
#include <QStringList>
#include <algorithm>

namespace studio::styx {

// 9P open modes
static constexpr uint8_t OREAD  = 0;
static constexpr uint8_t OWRITE = 1;

// Minimum header overhead for Tread/Twrite: size[4]+type[1]+tag[2]+fid[4]+offset[8]+count[4] = 23
// Rread overhead: size[4]+type[1]+tag[2]+count[4] = 11; data payload is msize-11
// Use 24 as a safe IOHDRSZ to leave room for both directions.
static constexpr uint32_t IOHDRSZ = 24u;

Styx9pClient::Styx9pClient(ITransport* transport, int timeoutMs)
    : m_transport(transport)
    , m_timeoutMs(timeoutMs)
{}

bool Styx9pClient::connectSession(uint32_t msize, const QString& version)
{
    const QByteArray tmsg = encodeTversion(kNoTag, msize, version);
    RMessage r;
    if (!transact(tmsg, MsgType::Rversion, r))
        return false;

    m_msize = r.msize;
    return true;
}

bool Styx9pClient::attach(const QString& uname, const QString& aname)
{
    const QByteArray tmsg = encodeTattach(m_nextTag++, m_rootFid, kNoFid, uname, aname);
    RMessage r;
    return transact(tmsg, MsgType::Rattach, r);
}

uint32_t Styx9pClient::negotiatedMsize() const
{
    return m_msize;
}

QString Styx9pClient::lastError() const
{
    return m_lastError;
}

bool Styx9pClient::walkTo(const QString& path, uint32_t& outFid)
{
    QStringList segments = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);

    const uint32_t newFid = allocFid();
    const uint16_t tag    = m_nextTag++;
    const QByteArray tmsg = encodeTwalk(tag, m_rootFid, newFid, segments);

    RMessage r;
    if (!transact(tmsg, MsgType::Rwalk, r)) {
        // fid was never fully established; no clunk needed (server never acked it)
        return false;
    }

    // Verify all segments were walked
    if (static_cast<int>(r.wqids.size()) != segments.size() && !segments.isEmpty()) {
        m_lastError = QStringLiteral("Rwalk: partial walk (%1/%2 segments)")
                          .arg(r.wqids.size())
                          .arg(segments.size());
        return false;
    }

    outFid = newFid;
    return true;
}

bool Styx9pClient::openFid(uint32_t fid, uint8_t mode, uint32_t& iounit)
{
    const uint16_t tag    = m_nextTag++;
    const QByteArray tmsg = encodeTopen(tag, fid, mode);
    RMessage r;
    if (!transact(tmsg, MsgType::Ropen, r))
        return false;
    iounit = r.iounit;
    return true;
}

bool Styx9pClient::readFid(uint32_t fid, uint64_t offset, uint32_t count,
                            QByteArray& out)
{
    const uint16_t tag    = m_nextTag++;
    const QByteArray tmsg = encodeTread(tag, fid, offset, count);
    RMessage r;
    if (!transact(tmsg, MsgType::Rread, r))
        return false;
    out = r.data;
    return true;
}

bool Styx9pClient::writeFid(uint32_t fid, uint64_t offset, const QByteArray& data,
                             uint32_t& written)
{
    const uint16_t tag    = m_nextTag++;
    const QByteArray tmsg = encodeTwrite(tag, fid, offset, data);
    RMessage r;
    if (!transact(tmsg, MsgType::Rwrite, r))
        return false;
    written = r.count;
    return true;
}

bool Styx9pClient::clunk(uint32_t fid)
{
    const uint16_t tag    = m_nextTag++;
    const QByteArray tmsg = encodeTclunk(tag, fid);
    RMessage r;
    return transact(tmsg, MsgType::Rclunk, r);
}

bool Styx9pClient::readPath(const QString& path, QByteArray& out, int maxBytes)
{
    uint32_t fid = 0;
    if (!walkTo(path, fid))
        return false;

    uint32_t iounit = 0;
    if (!openFid(fid, OREAD, iounit)) {
        clunk(fid); // best-effort
        return false;
    }

    // Choose chunk size: min(iounit (if >0), msize - IOHDRSZ)
    const uint32_t msizeAvail = (m_msize > IOHDRSZ) ? (m_msize - IOHDRSZ) : 512u;
    const uint32_t chunkSize  = (iounit > 0)
                                    ? std::min(iounit, msizeAvail)
                                    : msizeAvail;

    out.clear();
    uint64_t offset = 0;
    bool ok = true;

    while (static_cast<int>(out.size()) < maxBytes) {
        const uint32_t remaining =
            static_cast<uint32_t>(maxBytes - static_cast<int>(out.size()));
        const uint32_t toRead = std::min(chunkSize, remaining);

        QByteArray chunk;
        if (!readFid(fid, offset, toRead, chunk)) {
            ok = false;
            break;
        }
        if (chunk.isEmpty())
            break; // EOF

        out.append(chunk);
        offset += static_cast<uint64_t>(chunk.size());
    }

    clunk(fid); // best-effort
    return ok;
}

bool Styx9pClient::writePath(const QString& path, const QByteArray& data)
{
    uint32_t fid = 0;
    if (!walkTo(path, fid))
        return false;

    uint32_t iounit = 0;
    if (!openFid(fid, OWRITE, iounit)) {
        clunk(fid); // best-effort
        return false;
    }

    uint32_t written = 0;
    if (!writeFid(fid, 0, data, written)) {
        clunk(fid); // best-effort
        return false;
    }

    return clunk(fid);
}

bool Styx9pClient::transact(const QByteArray& tmsg, MsgType expectedR, RMessage& out)
{
    if (!m_transport->send(tmsg)) {
        m_lastError = QStringLiteral("transport send failed");
        return false;
    }

    QByteArray raw;
    if (!m_transport->recv(raw, m_timeoutMs)) {
        m_lastError = QStringLiteral("transport recv timeout or error");
        return false;
    }

    QString decodeErr;
    if (!decodeR(raw, out, &decodeErr)) {
        m_lastError = QStringLiteral("decode error: %1").arg(decodeErr);
        return false;
    }

    if (out.type == MsgType::Rerror) {
        m_lastError = out.ename;
        return false;
    }

    if (out.type != expectedR) {
        m_lastError = QStringLiteral("unexpected R-message type %1 (expected %2)")
                          .arg(static_cast<int>(out.type))
                          .arg(static_cast<int>(expectedR));
        return false;
    }

    // Tag validation: for Tversion we used kNoTag; for all others check match.
    // The sent tag is in bytes [5..6] of tmsg.
    if (tmsg.size() >= 7) {
        const auto* b = reinterpret_cast<const uint8_t*>(tmsg.constData());
        const uint16_t sentTag =
            static_cast<uint16_t>(b[5]) | (static_cast<uint16_t>(b[6]) << 8);
        if (sentTag != kNoTag && out.tag != sentTag) {
            qWarning() << "[Styx9pClient] tag mismatch: sent" << sentTag
                       << "got" << out.tag;
        }
    }

    return true;
}

uint32_t Styx9pClient::allocFid()
{
    return m_nextFid++;
}

} // namespace studio::styx
