#include <QtTest/QtTest>
#include "core/acquisition/FrameParser.h"
#include "core/acquisition/EegFrame.h"

using namespace studio;

// Helper: build a valid 34-byte frame
static QByteArray buildFrame(uint32_t seq, const int32_t ch[8],
                              uint8_t p, uint8_t n, uint8_t g)
{
    QByteArray f(34, 0);
    // Sync
    f[0] = 0xA5;
    f[1] = 0x5A;
    // seq little-endian
    f[2] = static_cast<char>(seq & 0xFF);
    f[3] = static_cast<char>((seq >> 8) & 0xFF);
    f[4] = static_cast<char>((seq >> 16) & 0xFF);
    f[5] = static_cast<char>((seq >> 24) & 0xFF);
    // 8 channels big-endian int24
    for (int c = 0; c < 8; ++c) {
        int32_t v = ch[c];
        f[6 + c * 3 + 0] = static_cast<char>((v >> 16) & 0xFF);
        f[6 + c * 3 + 1] = static_cast<char>((v >> 8) & 0xFF);
        f[6 + c * 3 + 2] = static_cast<char>(v & 0xFF);
    }
    f[30] = static_cast<char>(p);
    f[31] = static_cast<char>(n);
    f[32] = static_cast<char>(g);
    // CRC over bytes [2..32]
    f[33] = static_cast<char>(FrameParser::crc8(
        reinterpret_cast<const uint8_t*>(f.constData()) + 2, 31));
    return f;
}

class TestFrameParser : public QObject
{
    Q_OBJECT

private slots:
    void parsesValidFrameSplitAcrossFeeds()
    {
        int32_t ch[8];
        for (int c = 0; c < 8; ++c)
            ch[c] = c * 1000 - 500;   // ch[0]=-500 (negative), ch[1]=500, ...

        QByteArray frame = buildFrame(42u, ch, 0x11, 0x22, 0x33);
        QCOMPARE(frame.size(), 34);

        FrameParser parser;
        parser.feed(frame.left(20));
        QVERIFY(parser.takeFrames().isEmpty());  // not enough data yet

        parser.feed(frame.mid(20));
        QVector<EegFrame> frames = parser.takeFrames();

        QCOMPARE(frames.size(), 1);
        const EegFrame& f = frames[0];
        QCOMPARE(f.seq, 42u);
        for (int c = 0; c < 8; ++c)
            QCOMPARE(f.ch[c], ch[c]);
        QCOMPARE(f.statP, uint8_t(0x11));
        QCOMPARE(f.statN, uint8_t(0x22));
        QCOMPARE(f.gpio,  uint8_t(0x33));
        QCOMPARE(parser.crcErrors(), uint64_t(0));
        QCOMPARE(parser.resyncs(),   uint64_t(0));
    }

    void corruptCrcCountsErrorNoFrame()
    {
        int32_t ch[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        QByteArray frame = buildFrame(1u, ch, 0, 0, 0);
        // Corrupt a data byte (not the sync)
        frame[10] = static_cast<char>(frame[10] ^ 0xFF);

        FrameParser parser;
        parser.feed(frame);
        QVector<EegFrame> frames = parser.takeFrames();

        QVERIFY(frames.isEmpty());
        QCOMPARE(parser.crcErrors(), uint64_t(1));
    }

    void resyncsAfterLeadingGarbage()
    {
        int32_t ch[8] = {100, 200, 300, 400, 500, 600, 700, 800};
        QByteArray frame = buildFrame(7u, ch, 0xAA, 0xBB, 0xCC);

        // 5 garbage bytes that don't contain 0xA5 0x5A together
        QByteArray garbage;
        garbage.append(char(0x00));
        garbage.append(char(0x11));
        garbage.append(char(0x22));
        garbage.append(char(0x33));
        garbage.append(char(0x44));

        FrameParser parser;
        parser.feed(garbage + frame);
        QVector<EegFrame> frames = parser.takeFrames();

        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].seq, 7u);
        QVERIFY(parser.resyncs() >= uint64_t(1));
    }

    void twoBackToBackFrames()
    {
        int32_t ch1[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        int32_t ch2[8] = {-1, -2, -3, -4, -5, -6, -7, -8};

        QByteArray data = buildFrame(10u, ch1, 0x01, 0x02, 0x03)
                        + buildFrame(11u, ch2, 0x04, 0x05, 0x06);

        FrameParser parser;
        parser.feed(data);
        QVector<EegFrame> frames = parser.takeFrames();

        QCOMPARE(frames.size(), 2);
        QCOMPARE(frames[0].seq, 10u);
        QCOMPARE(frames[1].seq, 11u);
        for (int c = 0; c < 8; ++c) {
            QCOMPARE(frames[0].ch[c], ch1[c]);
            QCOMPARE(frames[1].ch[c], ch2[c]);
        }
    }
};

QTEST_MAIN(TestFrameParser)
#include "test_frameparser.moc"
