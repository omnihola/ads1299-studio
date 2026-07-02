#include <QtTest/QtTest>
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <cstdint>

#include "core/acquisition/mmb0/ITransport.h"
#include "core/acquisition/mmb0/Styx9p.h"
#include "core/acquisition/mmb0/Styx9pClient.h"

// ── Local R-message encoder helpers ──────────────────────────────────────────
// Build raw little-endian 9P R-message bytes (size[4] type[1] tag[2] body...)

namespace {

using namespace studio::styx;

static void put8(QByteArray& b, uint8_t v)  { b.append(char(v)); }
static void put16(QByteArray& b, uint16_t v) { b.append(char(v&0xFF)); b.append(char(v>>8)); }
static void put32(QByteArray& b, uint32_t v) {
    b.append(char(v&0xFF)); b.append(char((v>>8)&0xFF));
    b.append(char((v>>16)&0xFF)); b.append(char((v>>24)&0xFF));
}
static void put64(QByteArray& b, uint64_t v) {
    for (int i=0;i<8;++i) b.append(char((v>>(i*8))&0xFF));
}
static void putStr(QByteArray& b, const QString& s) {
    QByteArray u = s.toUtf8();
    put16(b, uint16_t(u.size()));
    b.append(u);
}
static void putQid(QByteArray& b, uint8_t type=0, uint32_t ver=0, uint64_t path=0) {
    put8(b,type); put32(b,ver); put64(b,path);
}
static void patchSize(QByteArray& b) {
    uint32_t sz = uint32_t(b.size());
    auto* d = reinterpret_cast<uint8_t*>(b.data());
    d[0]=sz&0xFF; d[1]=(sz>>8)&0xFF; d[2]=(sz>>16)&0xFF; d[3]=(sz>>24)&0xFF;
}
static QByteArray makeHdr(uint8_t type, uint16_t tag) {
    QByteArray b; put32(b,0); put8(b,type); put16(b,tag); return b;
}

static QByteArray encodeRversion(uint16_t tag, uint32_t msize, const QString& ver) {
    QByteArray b = makeHdr(uint8_t(MsgType::Rversion), tag);
    put32(b, msize); putStr(b, ver); patchSize(b); return b;
}
static QByteArray encodeRattach(uint16_t tag) {
    QByteArray b = makeHdr(uint8_t(MsgType::Rattach), tag);
    putQid(b,0x80,0,1); patchSize(b); return b;
}
static QByteArray encodeRwalk(uint16_t tag, uint16_t nwqid) {
    QByteArray b = makeHdr(uint8_t(MsgType::Rwalk), tag);
    put16(b, nwqid);
    for (uint16_t i=0;i<nwqid;++i) putQid(b,0,0,uint64_t(i)+10);
    patchSize(b); return b;
}
static QByteArray encodeRopen(uint16_t tag, uint32_t iounit=2048) {
    QByteArray b = makeHdr(uint8_t(MsgType::Ropen), tag);
    putQid(b,0,0,42); put32(b,iounit); patchSize(b); return b;
}
static QByteArray encodeRwrite(uint16_t tag, uint32_t count) {
    QByteArray b = makeHdr(uint8_t(MsgType::Rwrite), tag);
    put32(b,count); patchSize(b); return b;
}
static QByteArray encodeRread(uint16_t tag, const QByteArray& data) {
    QByteArray b = makeHdr(uint8_t(MsgType::Rread), tag);
    put32(b, uint32_t(data.size())); b.append(data); patchSize(b); return b;
}
static QByteArray encodeRclunk(uint16_t tag) {
    QByteArray b = makeHdr(uint8_t(MsgType::Rclunk), tag);
    patchSize(b); return b;
}
static QByteArray encodeRerror(uint16_t tag, const QString& msg) {
    QByteArray b = makeHdr(uint8_t(MsgType::Rerror), tag);
    putStr(b, msg); patchSize(b); return b;
}

// ── Extract tag from a raw 9P T-message ─────────────────────────────────────
static uint16_t extractTag(const QByteArray& msg) {
    if (msg.size() < 7) return 0;
    const auto* b = reinterpret_cast<const uint8_t*>(msg.constData());
    return uint16_t(b[5]) | (uint16_t(b[6]) << 8);
}

static uint8_t extractType(const QByteArray& msg) {
    if (msg.size() < 5) return 0;
    return reinterpret_cast<const uint8_t*>(msg.constData())[4];
}

// ── MockStyxServer ────────────────────────────────────────────────────────────

class MockStyxServer : public ITransport {
public:
    QMap<QString, QByteArray> files;
    QMap<uint32_t, QString>   fidPath;
    bool errorOnWalk = false;   // when true, Twalk always returns Rerror
    int forcedWriteCount = -1;  // >=0 forces Rwrite.count for short-write tests

    bool send(const QByteArray& msg) override {
        m_pending = msg;
        return true;
    }

    bool recv(QByteArray& out, int /*timeoutMs*/) override {
        if (m_pending.isEmpty()) {
            out.clear();
            return false;
        }

        const uint8_t  type = extractType(m_pending);
        const uint16_t tag  = extractTag(m_pending);
        const auto*    b    = reinterpret_cast<const uint8_t*>(m_pending.constData());
        const auto*    end  = b + m_pending.size();

        switch (MsgType(type)) {

        case MsgType::Tversion: {
            // body: msize[4] version[2+s]
            uint32_t msize = 0;
            if (m_pending.size() >= 11) {
                msize = uint32_t(b[7])|(uint32_t(b[8])<<8)|(uint32_t(b[9])<<16)|(uint32_t(b[10])<<24);
            }
            QString ver;
            if (m_pending.size() > 11) {
                uint16_t vlen = uint16_t(b[11])|(uint16_t(b[12])<<8);
                ver = QString::fromUtf8(reinterpret_cast<const char*>(b+13), vlen);
            }
            // Cap msize at 8192
            msize = qMin(msize, uint32_t(8192));
            out = encodeRversion(tag, msize, ver);
            break;
        }

        case MsgType::Tattach: {
            out = encodeRattach(tag);
            break;
        }

        case MsgType::Twalk: {
            if (errorOnWalk) {
                out = encodeRerror(tag, QStringLiteral("no such file or directory"));
                break;
            }
            // Parse fid[4] newfid[4] nwname[2] wname[s]*
            if (m_pending.size() < 17) { out.clear(); return false; }
            const auto* cur = b + 7;
            uint32_t fid    = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24); cur+=4;
            uint32_t newfid = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24); cur+=4;
            uint16_t nwname = uint16_t(cur[0])|(uint16_t(cur[1])<<8); cur+=2;

            // Build new path by appending segments to the path of fid
            QString basePath = fidPath.value(fid, QStringLiteral(""));
            QStringList parts;
            for (uint16_t i=0;i<nwname;++i) {
                if (cur+2 > end) break;
                uint16_t slen = uint16_t(cur[0])|(uint16_t(cur[1])<<8); cur+=2;
                if (cur+slen > end) break;
                parts << QString::fromUtf8(reinterpret_cast<const char*>(cur), slen);
                cur += slen;
            }

            QString newPath = basePath;
            for (const QString& p : parts) {
                if (!newPath.isEmpty() && !newPath.endsWith(QLatin1Char('/')))
                    newPath += QLatin1Char('/');
                newPath += p;
            }
            if (!newPath.startsWith(QLatin1Char('/')))
                newPath.prepend(QLatin1Char('/'));

            fidPath[newfid] = newPath;
            out = encodeRwalk(tag, uint16_t(parts.size()));
            break;
        }

        case MsgType::Topen: {
            out = encodeRopen(tag, 2048);
            break;
        }

        case MsgType::Twrite: {
            // fid[4] offset[8] count[4] data[count]
            if (m_pending.size() < 23) { out.clear(); return false; }
            const auto* cur = b + 7;
            uint32_t fid = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24); cur+=4;
            cur += 8; // skip offset
            uint32_t count = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24); cur+=4;
            QByteArray data(reinterpret_cast<const char*>(cur), int(count));
            QString path = fidPath.value(fid, QString());
            if (!path.isEmpty())
                files[path] = data;
            const uint32_t replyCount = forcedWriteCount >= 0
                                            ? static_cast<uint32_t>(forcedWriteCount)
                                            : count;
            out = encodeRwrite(tag, replyCount);
            break;
        }

        case MsgType::Tread: {
            // fid[4] offset[8] count[4]
            if (m_pending.size() < 23) { out.clear(); return false; }
            const auto* cur = b + 7;
            uint32_t fid = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24); cur+=4;
            uint64_t offset = 0;
            for (int i=0;i<8;++i) offset |= uint64_t(cur[i])<<(i*8); cur+=8;
            uint32_t count = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24);
            QString path = fidPath.value(fid, QString());
            QByteArray fileData = files.value(path);
            QByteArray slice;
            if (offset < uint64_t(fileData.size())) {
                int start = int(offset);
                int len   = qMin(int(count), fileData.size() - start);
                slice = fileData.mid(start, len);
            }
            out = encodeRread(tag, slice);
            break;
        }

        case MsgType::Tclunk: {
            // fid[4]
            if (m_pending.size() >= 11) {
                const auto* cur = b + 7;
                uint32_t fid = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24);
                fidPath.remove(fid);
            }
            out = encodeRclunk(tag);
            break;
        }

        default:
            out.clear();
            return false;
        }

        m_pending.clear();
        return true;
    }

private:
    QByteArray m_pending;
};

} // anonymous namespace

// ── Test class ────────────────────────────────────────────────────────────────

class TestStyx9pClient : public QObject {
    Q_OBJECT

private slots:
    void handshakeAndAttach() {
        MockStyxServer mock;
        studio::styx::Styx9pClient client(&mock, 100);

        QVERIFY(client.connectSession());
        QVERIFY(client.negotiatedMsize() > 0);
        QVERIFY(client.attach());
    }

    void writeThenReadBack() {
        MockStyxServer mock;
        studio::styx::Styx9pClient client(&mock, 100);
        QVERIFY(client.connectSession());
        QVERIFY(client.attach());

        const QByteArray payload(1, char(0x96));
        QVERIFY(client.writePath(QStringLiteral("/ads1299evm/conf/config1"), payload));

        QByteArray out;
        QVERIFY(client.readPath(QStringLiteral("/ads1299evm/conf/config1"), out));
        QCOMPARE(out, payload);
    }

    void readDataNode() {
        MockStyxServer mock;
        // Pre-seed the file with 54 bytes (two 27-byte records)
        QByteArray seed(54, 0);
        for (int i = 0; i < 54; ++i)
            seed[i] = char(i);
        mock.files[QStringLiteral("/ads1299evm/data")] = seed;

        studio::styx::Styx9pClient client(&mock, 100);
        QVERIFY(client.connectSession());
        QVERIFY(client.attach());

        QByteArray out;
        QVERIFY(client.readPath(QStringLiteral("/ads1299evm/data"), out));
        QCOMPARE(out, seed);
    }

    void errorPropagates() {
        MockStyxServer mock;
        mock.errorOnWalk = true;

        studio::styx::Styx9pClient client(&mock, 100);
        QVERIFY(client.connectSession());
        QVERIFY(client.attach());

        bool result = client.writePath(QStringLiteral("/nonexistent/path"),
                                       QByteArray(1, 0x42));
        QVERIFY(!result);
        QVERIFY(!client.lastError().isEmpty());
    }

    void shortWriteFails() {
        MockStyxServer mock;
        mock.forcedWriteCount = 1;

        studio::styx::Styx9pClient client(&mock, 100);
        QVERIFY(client.connectSession());
        QVERIFY(client.attach());

        const bool result = client.writePath(QStringLiteral("/ads1299evm/conf/config1"),
                                             QByteArrayLiteral("0x96"));
        QVERIFY(!result);
        QVERIFY2(client.lastError().contains(QStringLiteral("short write")),
                 qPrintable(client.lastError()));
    }
};

QTEST_MAIN(TestStyx9pClient)
#include "test_styx9pclient.moc"
