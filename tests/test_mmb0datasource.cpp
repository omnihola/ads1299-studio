// tests/test_mmb0datasource.cpp
// TDD tests for Mmb0DataSource (Phase 8, Task 6)
#include <QtTest>
#include <QSignalSpy>
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <atomic>

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

// ── Build synthetic 27-byte EEG record ──────────────────────────────────────
// status: 0xC0 0x00 0x00, each channel 3 bytes big-endian = (counter*8+ch)
static QByteArray makeSyntheticRecord(int counter) {
    QByteArray rec(27, '\0');
    // status bytes
    rec[0] = char(0xC0);
    rec[1] = char(0x00);
    rec[2] = char(0x00);
    for (int ch = 0; ch < 8; ++ch) {
        int32_t val = counter * 8 + ch;
        int base = 3 + ch * 3;
        rec[base+0] = char((val >> 16) & 0xFF);
        rec[base+1] = char((val >> 8)  & 0xFF);
        rec[base+2] = char( val        & 0xFF);
    }
    return rec;
}

// ── FakeMmb0Transport ─────────────────────────────────────────────────────────
// Full 9P2000 server that also:
//   - stores writes to conf/*, blocksize_samples, acquire nodes
//   - for reads of the data path, returns blocksizeSamples synthetic records

static constexpr int kFakeChunkSize = 200;  // max bytes returned per Rread

class FakeMmb0Transport : public ITransport {
public:
    QMap<QString, QByteArray> files;   // path -> last written bytes
    QMap<uint32_t, QString>   fidPath; // fid -> resolved path
    int blocksizeSamples = 3;          // default; updated when client writes blocksize
    int sampleCounter = 0;             // increments with each read of /data

    // Streaming position: we build the full block once per logical poll and
    // serve it in kFakeChunkSize slices so pollOnce must loop.
    QByteArray dataBlock;              // current block being served
    int        dataBlockPos = 0;       // bytes consumed from dataBlock so far

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
            uint32_t msize = 0;
            if (m_pending.size() >= 11) {
                msize = uint32_t(b[7])|(uint32_t(b[8])<<8)|(uint32_t(b[9])<<16)|(uint32_t(b[10])<<24);
            }
            QString ver;
            if (m_pending.size() > 11) {
                uint16_t vlen = uint16_t(b[11])|(uint16_t(b[12])<<8);
                ver = QString::fromUtf8(reinterpret_cast<const char*>(b+13), vlen);
            }
            msize = qMin(msize, uint32_t(8192));
            out = encodeRversion(tag, msize, ver);
            break;
        }

        case MsgType::Tattach: {
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
            out = encodeRopen(tag, 200);  // advertise small iounit to exercise drain loop
            break;
        }

        case MsgType::Twrite: {
            if (m_pending.size() < 23) { out.clear(); return false; }
            const auto* cur = b + 7;
            uint32_t fid = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24); cur+=4;
            cur += 8; // skip offset
            uint32_t count = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24); cur+=4;
            QByteArray data(reinterpret_cast<const char*>(cur), int(count));
            QString path = fidPath.value(fid, QString());
            if (!path.isEmpty())
                files[path] = data;
            out = encodeRwrite(tag, count);
            break;
        }

        case MsgType::Tread: {
            if (m_pending.size() < 23) { out.clear(); return false; }
            const auto* cur = b + 7;
            uint32_t fid   = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24); cur+=4;
            cur += 8; // skip offset[8]
            uint32_t count = uint32_t(cur[0])|(uint32_t(cur[1])<<8)|(uint32_t(cur[2])<<16)|(uint32_t(cur[3])<<24); cur+=4;
            (void)count;
            QString path = fidPath.value(fid, QString());

            QByteArray result;
            if (path == QStringLiteral("/ads1299evm/data")) {
                // Build a new full block when the previous one has been drained.
                if (dataBlock.isEmpty() || dataBlockPos >= dataBlock.size()) {
                    dataBlock.clear();
                    for (int i = 0; i < blocksizeSamples; ++i)
                        dataBlock.append(makeSyntheticRecord(sampleCounter++));
                    dataBlockPos = 0;
                }
                // Return at most kFakeChunkSize bytes — forces pollOnce to loop.
                int avail = dataBlock.size() - dataBlockPos;
                int take  = qMin(avail, kFakeChunkSize);
                result    = dataBlock.mid(dataBlockPos, take);
                dataBlockPos += take;
                // Reset block once fully consumed so next poll gets fresh data.
                if (dataBlockPos >= dataBlock.size()) {
                    dataBlock.clear();
                    dataBlockPos = 0;
                }
            } else {
                result = files.value(path);
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
            out.clear();
            return false;
        }

        m_pending.clear();
        return true;
    }

private:
    QByteArray m_pending;
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

    // Test 1: bringUp writes registers and sets acquire=1
    void bringUpWritesRegisters() {
        auto* fake = new FakeMmb0Transport();
        fake->blocksizeSamples = 2;

        studio::DeviceConfig cfg = studio::DeviceConfig().withSampleRate(500);
        auto* src = new studio::mmb0::Mmb0DataSource(fake);
        src->setConfig(cfg);
        src->setBlocksizeSamples(2);

        src->start();
        QTest::qWait(50);

        // config1 at register index 0 should have been written
        QVERIFY2(fake->files.contains(QStringLiteral("/ads1299evm/conf/config1")),
                 "Expected write to /ads1299evm/conf/config1");

        // acquire should have been written =1
        QVERIFY2(fake->files.contains(QStringLiteral("/ads1299evm/acquire")),
                 "Expected write to /ads1299evm/acquire");
        QCOMPARE(fake->files[QStringLiteral("/ads1299evm/acquire")],
                 QByteArray(1, '\x01'));

        QVERIFY(src->isRunning());

        src->stop();
        delete src;
    }

    // Test 2: streams frames from data node; acquire goes to 0 after stop
    void streamsFramesFromDataNode() {
        auto* fake = new FakeMmb0Transport();
        fake->blocksizeSamples = 3;

        auto* src = new studio::mmb0::Mmb0DataSource(fake);
        src->setBlocksizeSamples(3);

        QSignalSpy frameSpy(src, &studio::IDataSource::framesReady);

        src->start();
        QTest::qWait(100);

        // Should have emitted at least one batch of frames
        QVERIFY2(frameSpy.count() > 0, "framesReady signal not emitted");

        // Total frames across all batches should be > 0
        int totalFrames = 0;
        for (int i = 0; i < frameSpy.count(); ++i) {
            auto batch = frameSpy.at(i).at(0).value<studio::EegFrameBatch>();
            totalFrames += int(batch.size());

            // Each frame should have 8 channels
            for (const auto& frame : batch) {
                // Verify the synthetic pattern: ch[c] = counter*8+c (for small counters)
                // Just verify all 8 channels exist (non-default values)
                (void)frame; // pattern check below
            }
        }
        QVERIFY2(totalFrames > 0, "No frames received");

        // Verify first batch has correct channel pattern
        if (frameSpy.count() > 0) {
            auto firstBatch = frameSpy.at(0).at(0).value<studio::EegFrameBatch>();
            QVERIFY(!firstBatch.empty());
            // First frame, counter=0: ch[c] should be c (for c=0..7)
            const auto& f = firstBatch[0];
            for (int c = 0; c < 8; ++c) {
                QCOMPARE(f.ch[c], c); // counter=0, ch=c -> 0*8+c = c
            }
        }

        src->stop();
        QTest::qWait(10);

        // acquire should have been written =0 after stop
        QVERIFY2(fake->files.contains(QStringLiteral("/ads1299evm/acquire")),
                 "Expected acquire node to exist");
        QCOMPARE(fake->files[QStringLiteral("/ads1299evm/acquire")],
                 QByteArray(1, '\x00'));

        QVERIFY(!src->isRunning());

        delete src;
    }

    // Test 3: bringUp failure (recv fails) emits errorOccurred; isRunning stays false
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
};

QTEST_MAIN(TestMmb0DataSource)
#include "test_mmb0datasource.moc"
