#include "Styx9p.h"

namespace studio::styx {

// ── Little-endian put helpers ─────────────────────────────────────────────────

static void putU8(QByteArray& buf, uint8_t v)
{
    buf.append(static_cast<char>(v));
}

static void putU16(QByteArray& buf, uint16_t v)
{
    buf.append(static_cast<char>(v & 0xFF));
    buf.append(static_cast<char>((v >> 8) & 0xFF));
}

static void putU32(QByteArray& buf, uint32_t v)
{
    buf.append(static_cast<char>(v & 0xFF));
    buf.append(static_cast<char>((v >> 8) & 0xFF));
    buf.append(static_cast<char>((v >> 16) & 0xFF));
    buf.append(static_cast<char>((v >> 24) & 0xFF));
}

static void putU64(QByteArray& buf, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        buf.append(static_cast<char>((v >> (i * 8)) & 0xFF));
}

static void putString(QByteArray& buf, const QString& s)
{
    const QByteArray utf8 = s.toUtf8();
    putU16(buf, static_cast<uint16_t>(utf8.size()));
    buf.append(utf8);
}

static void putQid(QByteArray& buf, const Qid& q)
{
    putU8(buf, q.type);
    putU32(buf, q.version);
    putU64(buf, q.path);
}

// Back-patch size[4] at the start of a fully-assembled message buffer.
static void backpatchSize(QByteArray& buf)
{
    const uint32_t sz = static_cast<uint32_t>(buf.size());
    auto* b = reinterpret_cast<uint8_t*>(buf.data());
    b[0] = sz & 0xFF;
    b[1] = (sz >> 8) & 0xFF;
    b[2] = (sz >> 16) & 0xFF;
    b[3] = (sz >> 24) & 0xFF;
}

// Build the fixed 7-byte header: size[4](placeholder) type[1] tag[2].
static QByteArray makeHeader(MsgType type, uint16_t tag)
{
    QByteArray buf;
    buf.reserve(7);
    putU32(buf, 0u); // placeholder; back-patched later
    putU8(buf, static_cast<uint8_t>(type));
    putU16(buf, tag);
    return buf;
}

// ── Little-endian get helpers ─────────────────────────────────────────────────

static uint8_t getU8(const uint8_t* p) { return *p; }

static uint16_t getU16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t getU32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

static uint64_t getU64(const uint8_t* p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(p[i]) << (i * 8);
    return v;
}

// Read a 9P string[s] from cursor; advances cursor; returns false on overrun.
static bool getString(const uint8_t*& cur, const uint8_t* end, QString& out)
{
    if (cur + 2 > end)
        return false;
    const uint16_t len = getU16(cur);
    cur += 2;
    if (cur + len > end)
        return false;
    out = QString::fromUtf8(reinterpret_cast<const char*>(cur), len);
    cur += len;
    return true;
}

// Read a qid[13] from cursor; advances cursor; returns false on overrun.
static bool getQid(const uint8_t*& cur, const uint8_t* end, Qid& out)
{
    if (cur + 13 > end)
        return false;
    out.type    = getU8(cur);      cur += 1;
    out.version = getU32(cur);     cur += 4;
    out.path    = getU64(cur);     cur += 8;
    return true;
}

// ── Encoders ──────────────────────────────────────────────────────────────────

QByteArray encodeTversion(uint16_t tag, uint32_t msize, const QString& version)
{
    QByteArray buf = makeHeader(MsgType::Tversion, tag);
    putU32(buf, msize);
    putString(buf, version);
    backpatchSize(buf);
    return buf;
}

QByteArray encodeTattach(uint16_t tag, uint32_t fid, uint32_t afid,
                         const QString& uname, const QString& aname)
{
    QByteArray buf = makeHeader(MsgType::Tattach, tag);
    putU32(buf, fid);
    putU32(buf, afid);
    putString(buf, uname);
    putString(buf, aname);
    backpatchSize(buf);
    return buf;
}

QByteArray encodeTwalk(uint16_t tag, uint32_t fid, uint32_t newfid,
                       const QStringList& wnames)
{
    QByteArray buf = makeHeader(MsgType::Twalk, tag);
    putU32(buf, fid);
    putU32(buf, newfid);
    putU16(buf, static_cast<uint16_t>(wnames.size()));
    for (const QString& name : wnames)
        putString(buf, name);
    backpatchSize(buf);
    return buf;
}

QByteArray encodeTopen(uint16_t tag, uint32_t fid, uint8_t mode)
{
    QByteArray buf = makeHeader(MsgType::Topen, tag);
    putU32(buf, fid);
    putU8(buf, mode);
    backpatchSize(buf);
    return buf;
}

QByteArray encodeTread(uint16_t tag, uint32_t fid, uint64_t offset,
                       uint32_t count)
{
    QByteArray buf = makeHeader(MsgType::Tread, tag);
    putU32(buf, fid);
    putU64(buf, offset);
    putU32(buf, count);
    backpatchSize(buf);
    return buf;
}

QByteArray encodeTwrite(uint16_t tag, uint32_t fid, uint64_t offset,
                        const QByteArray& data)
{
    QByteArray buf = makeHeader(MsgType::Twrite, tag);
    putU32(buf, fid);
    putU64(buf, offset);
    putU32(buf, static_cast<uint32_t>(data.size()));
    buf.append(data);
    backpatchSize(buf);
    return buf;
}

QByteArray encodeTclunk(uint16_t tag, uint32_t fid)
{
    QByteArray buf = makeHeader(MsgType::Tclunk, tag);
    putU32(buf, fid);
    backpatchSize(buf);
    return buf;
}

// ── Decoder ───────────────────────────────────────────────────────────────────

bool decodeR(const QByteArray& msg, RMessage& out, QString* err)
{
    auto fail = [&](const char* reason) -> bool {
        if (err) *err = QString::fromLatin1(reason);
        return false;
    };

    if (msg.size() < 7)
        return fail("message too short (< 7 bytes)");

    const auto* b   = reinterpret_cast<const uint8_t*>(msg.constData());
    const auto* end = b + msg.size();

    const uint32_t declaredSize = getU32(b);
    if (static_cast<int>(declaredSize) != msg.size())
        return fail("size field does not match buffer length");

    const uint8_t typeByte = b[4];
    const uint16_t tag     = getU16(b + 5);

    out = RMessage{};
    out.tag = tag;

    const auto* cur = b + 7; // points to body start

    switch (typeByte) {

    case static_cast<uint8_t>(MsgType::Rversion): {
        out.type = MsgType::Rversion;
        if (cur + 4 > end) return fail("Rversion: truncated msize");
        out.msize = getU32(cur); cur += 4;
        if (!getString(cur, end, out.version)) return fail("Rversion: truncated version");
        break;
    }

    case static_cast<uint8_t>(MsgType::Rattach): {
        out.type = MsgType::Rattach;
        if (!getQid(cur, end, out.qid)) return fail("Rattach: truncated qid");
        break;
    }

    case static_cast<uint8_t>(MsgType::Rerror): {
        out.type = MsgType::Rerror;
        if (!getString(cur, end, out.ename)) return fail("Rerror: truncated ename");
        break;
    }

    case static_cast<uint8_t>(MsgType::Rwalk): {
        out.type = MsgType::Rwalk;
        if (cur + 2 > end) return fail("Rwalk: truncated nwqid");
        const uint16_t nwqid = getU16(cur); cur += 2;
        out.wqids.reserve(nwqid);
        for (uint16_t i = 0; i < nwqid; ++i) {
            Qid q{};
            if (!getQid(cur, end, q)) return fail("Rwalk: truncated qid");
            out.wqids.append(q);
        }
        break;
    }

    case static_cast<uint8_t>(MsgType::Ropen): {
        out.type = MsgType::Ropen;
        if (!getQid(cur, end, out.qid)) return fail("Ropen: truncated qid");
        if (cur + 4 > end) return fail("Ropen: truncated iounit");
        out.iounit = getU32(cur); cur += 4;
        break;
    }

    case static_cast<uint8_t>(MsgType::Rread): {
        out.type = MsgType::Rread;
        if (cur + 4 > end) return fail("Rread: truncated count");
        out.count = getU32(cur); cur += 4;
        if (cur + out.count > end) return fail("Rread: truncated data");
        out.data = QByteArray(reinterpret_cast<const char*>(cur), int(out.count));
        cur += out.count;
        break;
    }

    case static_cast<uint8_t>(MsgType::Rwrite): {
        out.type = MsgType::Rwrite;
        if (cur + 4 > end) return fail("Rwrite: truncated count");
        out.count = getU32(cur); cur += 4;
        break;
    }

    case static_cast<uint8_t>(MsgType::Rclunk): {
        out.type = MsgType::Rclunk;
        break;
    }

    default:
        return fail("unknown or unsupported R-message type");
    }

    return true;
}

// ── frameLength ───────────────────────────────────────────────────────────────

int frameLength(const QByteArray& buf)
{
    if (buf.size() < 4)
        return -1;
    const auto* b = reinterpret_cast<const uint8_t*>(buf.constData());
    return static_cast<int>(getU32(b));
}

} // namespace studio::styx
