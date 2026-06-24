#include <QtTest>
#include <QByteArray>
#include <QVector>
#include "core/acquisition/mmb0/Ads1299RecordParser.h"

using namespace studio::mmb0;

class TestAds1299Record : public QObject {
    Q_OBJECT

private slots:
    // ── parsesStatusAndChannels ───────────────────────────────────────────────
    // Status word for statP=0xA5, statN=0x3C, gpio=0x0F:
    //   bits 23-20 = 1100 (fixed header)
    //   bits 19-12 = 0xA5 = 1010 0101
    //   bits 11-4  = 0x3C = 0011 1100
    //   bits  3-0  = 0x0F = 1111
    //   => 1100 1010 0101 0011 1100 1111 = 0xCA 0x53 0xCF
    void parsesStatusAndChannels() {
        uint8_t rec[27] = {};

        // Status bytes: statP=0xA5, statN=0x3C, gpio=0x0F → 0xCA 0x53 0xCF
        rec[0] = 0xCA;
        rec[1] = 0x53;
        rec[2] = 0xCF;

        // ch0 = +1 → 0x00 0x00 0x01
        rec[3] = 0x00; rec[4] = 0x00; rec[5] = 0x01;

        // ch1 = -1 → 0xFF 0xFF 0xFF
        rec[6] = 0xFF; rec[7] = 0xFF; rec[8] = 0xFF;

        // ch2 = 0x7FFFFF (max positive) → 0x7F 0xFF 0xFF
        rec[9]  = 0x7F; rec[10] = 0xFF; rec[11] = 0xFF;

        // ch3 = 3000 → 0x00 0x0B 0xB8
        rec[12] = 0x00; rec[13] = 0x0B; rec[14] = 0xB8;

        // ch4..ch7 = c*1000 (4000..7000), leave as zero for simplicity beyond ch3
        // ch4 = 4000 → 0x00 0x0F 0xA0
        rec[15] = 0x00; rec[16] = 0x0F; rec[17] = 0xA0;
        // ch5 = 5000 → 0x00 0x13 0x88
        rec[18] = 0x00; rec[19] = 0x13; rec[20] = 0x88;
        // ch6 = 6000 → 0x00 0x17 0x70
        rec[21] = 0x00; rec[22] = 0x17; rec[23] = 0x70;
        // ch7 = 7000 → 0x00 0x1B 0x58
        rec[24] = 0x00; rec[25] = 0x1B; rec[26] = 0x58;

        studio::EegFrame f = Ads1299RecordParser::parseRecord(rec, 5);

        QCOMPARE(f.seq,   uint32_t(5));
        QCOMPARE(f.statP, uint8_t(0xA5));
        QCOMPARE(f.statN, uint8_t(0x3C));
        QCOMPARE(f.gpio,  uint8_t(0x0F));
        QCOMPARE(f.ch[0], int32_t(1));
        QCOMPARE(f.ch[1], int32_t(-1));
        QCOMPARE(f.ch[2], int32_t(8388607));
        QCOMPARE(f.ch[3], int32_t(3000));
    }

    // ── feedSplitAcrossCalls ──────────────────────────────────────────────────
    // Feed first 20 bytes of record 1, then remaining 7 bytes + full record 2.
    // takeFrames() must return 2 frames with seq 0 and 1.
    void feedSplitAcrossCalls() {
        // Build two identical 27-byte records (all zeros for simplicity)
        QByteArray rec1(27, 0);
        QByteArray rec2(27, 0);

        Ads1299RecordParser parser;

        // Feed first 20 bytes of record 1
        parser.feed(rec1.left(20));
        QVERIFY(parser.takeFrames().isEmpty());

        // Feed remaining 7 bytes of record 1 + full record 2
        parser.feed(rec1.mid(20) + rec2);

        QVector<studio::EegFrame> frames = parser.takeFrames();
        QCOMPARE(frames.size(), 2);
        QCOMPARE(frames[0].seq, uint32_t(0));
        QCOMPARE(frames[1].seq, uint32_t(1));
    }

    // ── partialRetained ───────────────────────────────────────────────────────
    // Feed 27+10 bytes; takeFrames() returns 1 frame.
    // A second feed of the missing 17 bytes completes the 2nd record (seq 1).
    void partialRetained() {
        QByteArray twoRecords(27 * 2, 0);

        Ads1299RecordParser parser;

        // Feed 37 bytes (27 + 10 partial bytes of second record)
        parser.feed(twoRecords.left(37));

        QVector<studio::EegFrame> frames = parser.takeFrames();
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].seq, uint32_t(0));

        // Feed remaining 17 bytes to complete second record
        parser.feed(twoRecords.mid(37));

        frames = parser.takeFrames();
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].seq, uint32_t(1));
    }

    // ── resetClears ───────────────────────────────────────────────────────────
    // After feeding some bytes, reset() → seq()==0 and takeFrames() is empty.
    void resetClears() {
        QByteArray rec(27, 0);

        Ads1299RecordParser parser;
        parser.feed(rec);
        // There should be a frame queued
        QCOMPARE(parser.takeFrames().size(), 1);

        // Feed partial bytes then reset
        parser.feed(rec.left(10));
        parser.reset();

        QCOMPARE(parser.seq(), uint32_t(0));
        QVERIFY(parser.takeFrames().isEmpty());

        // After reset, can parse again from seq 0
        parser.feed(rec);
        QVector<studio::EegFrame> frames = parser.takeFrames();
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].seq, uint32_t(0));
    }
};

QTEST_MAIN(TestAds1299Record)
#include "test_ads1299record.moc"
