#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <cstdint>

namespace studio::styx {

// ── Message type codes ────────────────────────────────────────────────────────
enum class MsgType : uint8_t {
    Tversion = 100,
    Rversion = 101,
    Tattach  = 104,
    Rattach  = 105,
    Rerror   = 107,
    Twalk    = 110,
    Rwalk    = 111,
    Topen    = 112,
    Ropen    = 113,
    Tread    = 116,
    Rread    = 117,
    Twrite   = 118,
    Rwrite   = 119,
    Tclunk   = 120,
    Rclunk   = 121,
};

// ── Special constants ─────────────────────────────────────────────────────────
inline constexpr uint32_t kNoFid = 0xFFFFFFFFu;
inline constexpr uint16_t kNoTag = 0xFFFFu;

// ── Qid ──────────────────────────────────────────────────────────────────────
struct Qid {
    uint8_t  type    = 0;
    uint32_t version = 0;
    uint64_t path    = 0;
};

// ── Decoded R-message ─────────────────────────────────────────────────────────
struct RMessage {
    MsgType  type    = MsgType::Rerror;
    uint16_t tag     = kNoTag;
    // Rversion / Tversion
    QString  version;
    uint32_t msize   = 0;
    // Rattach / Ropen
    Qid      qid{};
    uint32_t iounit  = 0;
    // Rwalk
    QVector<Qid> wqids;
    // Rread
    QByteArray data;
    uint32_t   count = 0;
    // Rerror
    QString ename;
};

// ── Encoders (return fully-framed QByteArray with size[4] prepended) ──────────
QByteArray encodeTversion(uint16_t tag, uint32_t msize, const QString& version);
QByteArray encodeTattach (uint16_t tag, uint32_t fid, uint32_t afid,
                          const QString& uname, const QString& aname);
QByteArray encodeTwalk   (uint16_t tag, uint32_t fid, uint32_t newfid,
                          const QStringList& wnames);
QByteArray encodeTopen   (uint16_t tag, uint32_t fid, uint8_t mode);
QByteArray encodeTread   (uint16_t tag, uint32_t fid, uint64_t offset,
                          uint32_t count);
QByteArray encodeTwrite  (uint16_t tag, uint32_t fid, uint64_t offset,
                          const QByteArray& data);
QByteArray encodeTclunk  (uint16_t tag, uint32_t fid);

// ── Decoder ───────────────────────────────────────────────────────────────────
// Parses ONE complete R-message from msg.
// Returns false (and sets *err if non-null) on malformed/short input.
bool decodeR(const QByteArray& msg, RMessage& out, QString* err = nullptr);

// ── Frame-length helper ───────────────────────────────────────────────────────
// Returns the total message length from the first 4 bytes of buf,
// or -1 if buf has fewer than 4 bytes.
int frameLength(const QByteArray& buf);

} // namespace studio::styx
