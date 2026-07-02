// tests/test_mmb0datasource.cpp
// TDD tests for Mmb0DataSource against the VERIFIED MMB0/estyx protocol
// (real-hardware findings, 2026-07-01 handoff):
//
//   - NO Tversion handshake: the reference client attaches directly with
//     uname/aname = "nobody"/"nobody".
//   - /ads1299evm/conf/* are estyx u32 files written as HEX TEXT ("0x95"),
//     not raw binary bytes.
//   - /ads1299evm/blocksize_samples is decimal text and counts 32-BIT WORDS
//     (9 words per EEG sample: status + ch1..ch8).
//   - /ads1299evm/acquire is a bool file written as TEXT "1"/"0". It is
//     ONE-SHOT: "1" collects exactly one block, then auto-resets to "0"
//     (firmware acquire.cpp: acquire_adc_done() → acquire_set(0)).
//     The host must poll it back to "0", read /data, and re-arm per block.
//
// FakeMmb0Transport below models exactly those semantics.

#include <QtTest>
#include <QSignalSpy>
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <cstdint>

#include "core/acquisition/mmb0/ITransport.h"
#include "core/acquisition/mmb0/Styx9p.h"
#include "core/acquisition/mmb0/Mmb0DataSource.h"
#include "core/device/DeviceConfig.h"
#include "core/acquisition/EegFrame.h"

// ── R-message encoder helpers (shared with test_styx9pclient.cpp pattern) ──────

namespace {

using namespace studio::styx;

static void put8(QByteArray& b, uint8_t v)  { b.append(char(v)); }
static void put16(QByteArray& b, uint16_t v) { b.append(char(v&0xFF)); b.append(char(v>>8)); }
static void put32(QByteArray& b, uint32_t v) {
    b.append(char(v&0xFF)); b.append(char((v>>8)&0xFF));
    b.append(char((v>>16)&0xFF)); b.append(char((v>>24)&0xFF));
}
static void putQid(QByteArray& b, uint8_t type=0, uint32_t ver=0, uint64_t path=0) {
    put8(b,type); put32(b,ver);
    for (int i=0;i<8;++i) b.append(char((path>>(i*8))&0xFF));
}
static void putStr(QByteArray& b, const QString& s) {
    QByteArray u = s.toUtf8();
    put16(b, uint16_t(u.size()));
    b.append(u);
}
static void patchSize(QByteArray& b) {
    uint32_t sz = uint32_t(b.size());
    auto* d = reinterpret_cast<uint8_t*>(b.data());
    d[0]=sz&0xFF; d[1]=(sz>>8)&0xFF; d[2]=(sz>>16)&0xFF; d[3]=(sz>>24)&0xFF;
}
static QByteArray makeHdr(uint8_t type, uint16_t tag) {
    QByteArray b; put32(b,0); put8(b,type); put16(b,tag); return b;
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
static QByteArray encodeRopen(uint16_t tag, uint32_t iounit=200) {
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

static uint16_t extractTag(const QByteArray& msg) {
    if (msg.size() < 7) return 0;
    const auto* b = reinterpret_cast<const uint8_t*>(msg.constData());
    return uint16_t(b[5]) | (uint16_t(b[6]) << 8);
}
static uint8_t extractType(const QByteArray& msg) {
    if (msg.size() < 5) return 0;
    return reinterpret_cast<const uint8_t*>(msg.constData())[4];
}

// ── FakeMmb0Transport ─────────────────────────────────────────────────────────
// 9P2000 server modelling the VERIFIED estyx firmware behavior (see header
// comment). Synthetic data: sample k has channel values ch[c] = k*8 + c,
// status word 0xC00000, packed as 9 BIG-endian 32-bit words per sample with
// the 24-bit value sign-extended to 32 bits — the layout confirmed by the
// 2026-07-02 raw hardware dump (internal test signal: status 0xFFC00000,
// channels 0x000141xx ≈ +82k codes).

class FakeMmb0Transport : public ITransport {
public:
    // Introspection for assertions
    QList<int>                typesSeen;         // every T-message type received
    QString                   attachUname;
    QString                   attachAname;
    QMap<QString, QByteArray> files;             // path -> last written payload
    QStringList               acquireWrites;     // every payload written to acquire
    int                       blocksizeWords = 576;
    bool                      acquireStuck   = false; // simulate dead DRDY
    uint32_t                  advertisedIounit = 200; // Ropen iounit reply
    uint32_t                  maxDataTreadCount = 0;  // largest /data Tread seen

    bool send(const QByteArray& msg) override {
        m_pending = msg;
        return true;
    }

    bool recv(QByteArray& out, int /*timeoutMs*/) override {
        if (m_pending.isEmpty()) { out.clear(); return false; }

        const uint8_t  type = extractType(m_pending);
        const uint16_t tag  = extractTag(m_pending);
        const auto*    b    = reinterpret_cast<const uint8_t*>(m_pending.constData());
        const auto*    end  = b + m_pending.size();
        typesSeen.append(int(type));

        switch (MsgType(type)) {

        case MsgType::Tversion: {
            // The real estyx firmware is not known to speak Tversion — the
            // verified client never sends it. Reply Rerror so any code path
            // that still depends on version negotiation fails loudly.
            out = encodeRerror(tag, QStringLiteral("Tversion unsupported"));
            break;
        }

        case MsgType::Tattach: {
            // body: fid[4] afid[4] uname[s] aname[s]
            const auto* cur = b + 7 + 4 + 4;
            auto readStr = [&](QString& s) {
                if (cur + 2 > end) return false;
                uint16_t len = uint16_t(cur[0]) | (uint16_t(cur[1]) << 8);
                cur += 2;
                if (cur + len > end) return false;
                s = QString::fromUtf8(reinterpret_cast<const char*>(cur), len);
                cur += len;
                return true;
            };
            readStr(attachUname);
            readStr(attachAname);
            out = encodeRattach(tag);
            break;
        }

        case MsgType::Twalk: {
            if (m_pending.size() < 17) { out.clear(); return false; }
            const auto* cur = b + 7;
            uint32_t fid    = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24); cur+=4;
            uint32_t newfid = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24); cur+=4;
            uint16_t nwname = uint16_t(cur[0])|(uint16_t(cur[1])<<8); cur+=2;

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
            out = encodeRopen(tag, advertisedIounit);
            break;
        }

        case MsgType::Twrite: {
            if (m_pending.size() < 23) { out.clear(); return false; }
            const auto* cur = b + 7;
            uint32_t fid = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24); cur+=4;
            cur += 8; // offset
            uint32_t count = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24); cur+=4;
            QByteArray data(reinterpret_cast<const char*>(cur), int(count));
            const QString path = fidPath.value(fid, QString());

            if (path == QStringLiteral("/ads1299evm/blocksize_samples")) {
                bool ok = false;
                const int words = data.trimmed().toInt(&ok);
                if (ok && words > 0) blocksizeWords = words;
                files[path] = data;
            } else if (path == QStringLiteral("/ads1299evm/acquire")) {
                acquireWrites << QString::fromUtf8(data);
                if (data.trimmed() == "1") {
                    if (acquireStuck) {
                        files[path] = QByteArrayLiteral("1"); // DRDY never fires
                    } else {
                        collectOneBlock();                     // instant DRDY
                        files[path] = QByteArrayLiteral("0"); // auto-reset
                    }
                } else {
                    files[path] = QByteArrayLiteral("0");
                }
            } else if (!path.isEmpty()) {
                files[path] = data;
            }
            out = encodeRwrite(tag, count);
            break;
        }

        case MsgType::Tread: {
            if (m_pending.size() < 23) { out.clear(); return false; }
            const auto* cur = b + 7;
            uint32_t fid   = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24); cur+=4;
            cur += 8; // offset
            uint32_t count = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24); cur+=4;
            const QString path = fidPath.value(fid, QString());

            QByteArray result;
            if (path == QStringLiteral("/ads1299evm/data")) {
                maxDataTreadCount = qMax(maxDataTreadCount, count);
                const int take = qMin<int>(int(count), dataQueue.size());
                result = dataQueue.left(take);
                dataQueue.remove(0, take);
            } else {
                result = files.value(path);
                if (int(count) < result.size())
                    result.truncate(int(count));
            }
            out = encodeRread(tag, result);
            break;
        }

        case MsgType::Tclunk: {
            if (m_pending.size() >= 11) {
                const auto* cur = b + 7;
                uint32_t fid = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24);
                fidPath.remove(fid);
            }
            out = encodeRclunk(tag);
            break;
        }

        default:
            out = encodeRerror(tag, QStringLiteral("unsupported message"));
            break;
        }

        m_pending.clear();
        return true;
    }

private:
    void collectOneBlock() {
        // One block = blocksizeWords 32-bit words = blocksizeWords/9 samples.
        const int samples = blocksizeWords / 9;
        for (int s = 0; s < samples; ++s) {
            appendWord(0xC00000u);                       // status
            for (int c = 0; c < 8; ++c)
                appendWord(uint32_t(sampleCounter * 8 + c) & 0xFFFFFFu);
            ++sampleCounter;
        }
    }
    // Big-endian, 24-bit value sign-extended to 32 bits (verified layout).
    void appendWord(uint32_t w24) {
        uint32_t w = w24 & 0xFFFFFFu;
        if (w & 0x800000u) w |= 0xFF000000u;   // sign extension as on hardware
        dataQueue.append(char((w >> 24) & 0xFF));
        dataQueue.append(char((w >> 16) & 0xFF));
        dataQueue.append(char((w >> 8) & 0xFF));
        dataQueue.append(char(w & 0xFF));
    }

    QMap<uint32_t, QString> fidPath;   // fid -> resolved path
    QByteArray              dataQueue; // pending /data bytes (32-bit words)
    int                     sampleCounter = 0;
    QByteArray              m_pending;
};

// ── Failing transport: recv() always returns false ────────────────────────────

class FailingTransport : public ITransport {
public:
    bool send(const QByteArray&) override { return true; }
    bool recv(QByteArray& out, int) override { out.clear(); return false; }
};

} // anonymous namespace

// ── Test class ────────────────────────────────────────────────────────────────

class TestMmb0DataSource : public QObject {
    Q_OBJECT

private slots:

    void initTestCase() {
        qRegisterMetaType<studio::EegFrameBatch>();
    }

    // bringUp must follow the verified wire protocol:
    // no Tversion, attach nobody/nobody, hex-text register writes,
    // word-count blocksize, text "1" acquire arm.
    void bringUpUsesVerifiedWireProtocol() {
        auto* fake = new FakeMmb0Transport();

        studio::DeviceConfig cfg = studio::DeviceConfig().withSampleRate(500);
        auto* src = new studio::mmb0::Mmb0DataSource(fake);
        src->setConfig(cfg);
        src->setBlocksizeSamples(4);   // 4 EEG samples = 36 words

        src->start();
        QTest::qWait(50);

        // The verified client never negotiates a version.
        QVERIFY2(!fake->typesSeen.contains(int(MsgType::Tversion)),
                 "Tversion must not be sent — estyx attaches directly");

        // Attach identity verified on hardware.
        QCOMPARE(fake->attachUname, QStringLiteral("nobody"));
        QCOMPARE(fake->attachAname, QStringLiteral("nobody"));

        // conf registers are u32 files written as hex text.
        QCOMPARE(fake->files.value(QStringLiteral("/ads1299evm/conf/config1")),
                 QByteArrayLiteral("0x95"));   // 0x90 | DR(500 SPS)
        QCOMPARE(fake->files.value(QStringLiteral("/ads1299evm/conf/config3")),
                 QByteArrayLiteral("0xEC"));   // bias enabled default

        // blocksize_samples counts 32-bit words (9 per EEG sample), decimal text.
        QCOMPARE(fake->files.value(QStringLiteral("/ads1299evm/blocksize_samples")),
                 QByteArrayLiteral("36"));

        // acquire armed with text "1".
        QVERIFY(!fake->acquireWrites.isEmpty());
        QCOMPARE(fake->acquireWrites.first(), QStringLiteral("1"));

        // /conf/reset must never be touched (hangs the Styx server).
        QVERIFY(!fake->files.contains(QStringLiteral("/ads1299evm/conf/reset")));

        QVERIFY(src->isRunning());

        src->stop();
        delete src;
    }

    // The firmware acquire is one-shot: streaming more than one block proves
    // the source re-arms acquire per block instead of writing it once.
    void streamsMultipleBlocksViaRearm() {
        auto* fake = new FakeMmb0Transport();

        auto* src = new studio::mmb0::Mmb0DataSource(fake);
        src->setBlocksizeSamples(2);   // 2 samples per block

        QSignalSpy frameSpy(src, &studio::IDataSource::framesReady);

        src->start();
        QTest::qWait(300);
        src->stop();

        int totalFrames = 0;
        for (int i = 0; i < frameSpy.count(); ++i)
            totalFrames += int(frameSpy.at(i).at(0).value<studio::EegFrameBatch>().size());

        QVERIFY2(totalFrames >= 4,
                 qPrintable(QStringLiteral("expected >= 2 blocks (4 frames), got %1 frames")
                                .arg(totalFrames)));

        // Re-arm evidence: acquire written "1" at least twice.
        QVERIFY2(fake->acquireWrites.count(QStringLiteral("1")) >= 2,
                 "acquire must be re-armed per block (one-shot firmware model)");

        // Synthetic pattern: sample k → ch[c] = k*8+c.
        const auto firstBatch = frameSpy.at(0).at(0).value<studio::EegFrameBatch>();
        QVERIFY(!firstBatch.empty());
        for (int c = 0; c < 8; ++c)
            QCOMPARE(firstBatch[0].ch[c], c);

        delete src;
    }

    // A block larger than the advertised iounit (200 B) forces readPath to
    // accumulate the block across multiple Rread round-trips. 10 samples =
    // 360 bytes → 200 + 160 chunks; the assembled frames must stay in order.
    void largeBlockSpansMultipleReads() {
        auto* fake = new FakeMmb0Transport();

        auto* src = new studio::mmb0::Mmb0DataSource(fake);
        src->setBlocksizeSamples(10);   // 360 bytes per block > iounit 200

        QSignalSpy frameSpy(src, &studio::IDataSource::framesReady);

        src->start();
        QTest::qWait(150);
        src->stop();

        QVERIFY2(frameSpy.count() > 0, "no frames from multi-chunk block");
        const auto firstBatch = frameSpy.at(0).at(0).value<studio::EegFrameBatch>();
        QCOMPARE(int(firstBatch.size()), 10);
        // Pattern ch[c] = k*8+c across the whole block proves the chunks were
        // appended in order (not overwritten or reordered).
        for (int k = 0; k < 10; ++k)
            for (int c = 0; c < 8; ++c)
                QCOMPARE(firstBatch[size_t(k)].ch[c], k * 8 + c);

        delete src;
    }

    // The estyx server's message buffer is ~2 KB: a /data Tread requesting
    // more than ~2037 payload bytes comes back with garbage past word ~509
    // (verified on hardware, 2026-07-02: a single 2304-byte read of a
    // 576-word block was junk after 509 words). The source must therefore
    // never request more than kMaxDataChunk bytes per Tread, even when the
    // server advertises no iounit limit.
    void dataTreadsRespectServerBufferCap() {
        auto* fake = new FakeMmb0Transport();
        fake->advertisedIounit = 0;    // server does not self-limit

        auto* src = new studio::mmb0::Mmb0DataSource(fake);
        src->setBlocksizeSamples(100); // 3600 bytes per block

        QSignalSpy frameSpy(src, &studio::IDataSource::framesReady);

        src->start();
        QTest::qWait(150);
        src->stop();

        QVERIFY2(fake->maxDataTreadCount > 0, "no /data Tread observed");
        QVERIFY2(fake->maxDataTreadCount <= 2016,
                 qPrintable(QStringLiteral("Tread count %1 exceeds the ~2KB estyx "
                                           "message buffer").arg(fake->maxDataTreadCount)));

        // The block must still assemble correctly across the capped chunks.
        QVERIFY(frameSpy.count() > 0);
        const auto firstBatch = frameSpy.at(0).at(0).value<studio::EegFrameBatch>();
        QCOMPARE(int(firstBatch.size()), 100);
        for (int c = 0; c < 8; ++c)
            QCOMPARE(firstBatch[0].ch[c], c);

        delete src;
    }

    // stop() must park the device with a text "0" write.
    void stopWritesAcquireZeroText() {
        auto* fake = new FakeMmb0Transport();
        auto* src = new studio::mmb0::Mmb0DataSource(fake);
        src->setBlocksizeSamples(2);

        src->start();
        QTest::qWait(60);
        src->stop();
        QTest::qWait(10);

        QVERIFY(!fake->acquireWrites.isEmpty());
        QCOMPARE(fake->acquireWrites.last(), QStringLiteral("0"));
        QVERIFY(!src->isRunning());

        delete src;
    }

    // bringUp failure (recv fails) emits errorOccurred; isRunning stays false.
    void bringUpFailureEmitsError() {
        auto* failing = new FailingTransport();
        auto* src = new studio::mmb0::Mmb0DataSource(failing);

        QSignalSpy errorSpy(src, &studio::IDataSource::errorOccurred);

        src->start();
        QTest::qWait(50);

        QVERIFY2(errorSpy.count() > 0, "errorOccurred not emitted on bringUp failure");
        QVERIFY2(!src->isRunning(), "isRunning should be false after failed bringUp");

        delete src;
    }

    // acquire stuck at "1" == DRDY never fires (board-level hardware condition
    // per the reverse-engineering findings). The source must time out with a
    // hardware-pointing error instead of polling forever.
    void stuckAcquireEmitsHardwareError() {
        auto* fake = new FakeMmb0Transport();
        fake->acquireStuck = true;

        auto* src = new studio::mmb0::Mmb0DataSource(fake);
        src->setBlocksizeSamples(2);
        src->setAcquireTimeoutMs(100);

        QSignalSpy errorSpy(src, &studio::IDataSource::errorOccurred);

        src->start();
        QTest::qWait(600);

        QVERIFY2(errorSpy.count() > 0, "expected timeout error for stuck acquire");
        const QString msg = errorSpy.at(0).at(0).toString();
        QVERIFY2(msg.contains(QStringLiteral("DRDY"), Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("error should point at DRDY hardware: %1").arg(msg)));
        QVERIFY(!src->isRunning());

        delete src;
    }
};

QTEST_MAIN(TestMmb0DataSource)
#include "test_mmb0datasource.moc"
