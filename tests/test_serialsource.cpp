// tests/test_serialsource.cpp
// TDD test for SerialSource — headless (no real port required).

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QMetaType>

#include "core/acquisition/EegFrame.h"
#include "core/acquisition/FrameParser.h"
#include "core/acquisition/SerialSource.h"

// ──────────────────────────────────────────────────────────────────────────────
// Helper: build a valid 34-byte frame
// ──────────────────────────────────────────────────────────────────────────────
static QByteArray buildFrame(uint32_t seq, const int32_t ch[8],
                              uint8_t p, uint8_t n, uint8_t g)
{
    QByteArray f(34, 0);
    f[0] = static_cast<char>(0xA5);
    f[1] = static_cast<char>(0x5A);
    // seq little-endian at bytes 2..5
    f[2] = static_cast<char>(seq & 0xFF);
    f[3] = static_cast<char>((seq >> 8)  & 0xFF);
    f[4] = static_cast<char>((seq >> 16) & 0xFF);
    f[5] = static_cast<char>((seq >> 24) & 0xFF);
    // 8 channels big-endian int24 at bytes 6..29
    for (int c = 0; c < 8; ++c) {
        int32_t v = ch[c];
        f[6 + c * 3 + 0] = static_cast<char>((v >> 16) & 0xFF);
        f[6 + c * 3 + 1] = static_cast<char>((v >> 8)  & 0xFF);
        f[6 + c * 3 + 2] = static_cast<char>( v        & 0xFF);
    }
    f[30] = static_cast<char>(p);
    f[31] = static_cast<char>(n);
    f[32] = static_cast<char>(g);
    // CRC over bytes [2..32]
    f[33] = static_cast<char>(studio::FrameParser::crc8(
        reinterpret_cast<const uint8_t*>(f.constData()) + 2, 31));
    return f;
}

class TestSerialSource : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<studio::EegFrameBatch>("studio::EegFrameBatch");
    }

    // ──────────────────────────────────────────────────────────────────────────
    // injectedFramesEmitted
    // Inject N=3 valid frames via feedBytesForTest and verify framesReady
    // carries exactly 3 frames total with correct seq and channel values.
    // ──────────────────────────────────────────────────────────────────────────
    void injectedFramesEmitted()
    {
        studio::SerialSource src;
        QSignalSpy spy(&src, &studio::SerialSource::framesReady);
        QVERIFY(spy.isValid());

        // Build 3 frames
        constexpr int N = 3;
        int32_t channels[3][8] = {
            { 100, 200, 300, 400, 500, 600, 700, 800 },
            { -100, -200, -300, -400, -500, -600, -700, -800 },
            { 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000 }
        };

        QByteArray buf;
        for (int i = 0; i < N; ++i) {
            buf += buildFrame(static_cast<uint32_t>(i), channels[i],
                              static_cast<uint8_t>(i + 1),
                              static_cast<uint8_t>(i + 2),
                              static_cast<uint8_t>(i + 3));
        }

        src.feedBytesForTest(buf);
        QCoreApplication::processEvents();

        // Count total frames across all emissions
        int total = 0;
        for (int i = 0; i < spy.count(); ++i) {
            const auto batch = spy.at(i).at(0).value<studio::EegFrameBatch>();
            total += static_cast<int>(batch.size());
        }
        QCOMPARE(total, N);

        // Verify first frame's seq and channels
        QVERIFY(spy.count() > 0);
        const auto firstBatch = spy.at(0).at(0).value<studio::EegFrameBatch>();
        QVERIFY(!firstBatch.empty());
        const studio::EegFrame& f0 = firstBatch.at(0);
        QCOMPARE(f0.seq, 0u);
        for (int c = 0; c < 8; ++c) {
            QCOMPARE(f0.ch[c], channels[0][c]);
        }
    }

    // ──────────────────────────────────────────────────────────────────────────
    // availablePortsDoesNotCrash
    // Just ensures the static method executes without crashing.
    // ──────────────────────────────────────────────────────────────────────────
    void availablePortsDoesNotCrash()
    {
        QStringList ports = studio::SerialSource::availablePorts();
        QVERIFY(ports.size() >= 0);  // tautologically true; proves no crash
    }
};

QTEST_MAIN(TestSerialSource)
#include "test_serialsource.moc"
