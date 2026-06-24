#include <QtTest/QtTest>
#include "core/acquisition/mmb0/Styx9p.h"

using namespace studio::styx;

class TestStyx9p : public QObject
{
    Q_OBJECT

private slots:

    // ── Known-vector: encodeTversion ─────────────────────────────────────────
    void tversion_knownVector()
    {
        // encodeTversion(kNoTag, 8192, "9P2000.USB.estyx")
        // size[4] type[1] tag[2] msize[4] strlen[2] "9P2000.USB.estyx"
        // = 4 + 1 + 2 + 4 + 2 + 16 = 29
        const QString ver = QStringLiteral("9P2000.USB.estyx");
        QByteArray msg = encodeTversion(kNoTag, 8192, ver);

        const int expectedSize = 4 + 1 + 2 + 4 + 2 + 16; // = 29
        QCOMPARE(msg.size(), expectedSize);

        const auto* b = reinterpret_cast<const uint8_t*>(msg.constData());

        // size[4] little-endian == 29
        uint32_t sz = uint32_t(b[0]) | (uint32_t(b[1]) << 8)
                    | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
        QCOMPARE(sz, uint32_t(expectedSize));

        // type == Tversion (100)
        QCOMPARE(b[4], uint8_t(100));

        // tag == kNoTag == 0xFFFF  (little-endian)
        QCOMPARE(b[5], uint8_t(0xFF));
        QCOMPARE(b[6], uint8_t(0xFF));

        // msize[4] little-endian == 8192
        uint32_t msize = uint32_t(b[7]) | (uint32_t(b[8]) << 8)
                       | (uint32_t(b[9]) << 16) | (uint32_t(b[10]) << 24);
        QCOMPARE(msize, uint32_t(8192));

        // string len[2] == 16
        uint16_t slen = uint16_t(b[11]) | (uint16_t(b[12]) << 8);
        QCOMPARE(slen, uint16_t(16));

        // 16 ASCII bytes
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(b + 13), 16),
                 QByteArray("9P2000.USB.estyx"));
    }

    // ── Round-trip: Rversion ────────────────────────────────────────────────
    void rversion_roundTrip()
    {
        const QString ver = QStringLiteral("9P2000");
        const uint16_t tag = 1;
        const uint32_t msize = 65536;
        // Manually build: size[4] type[1] tag[2] msize[4] strlen[2] bytes
        // total = 4+1+2+4+2+6 = 19
        QByteArray msg(19, 0);
        auto* b = reinterpret_cast<uint8_t*>(msg.data());
        b[0] = 19; b[1] = 0; b[2] = 0; b[3] = 0;   // size
        b[4] = 101;                                    // Rversion
        b[5] = tag & 0xFF; b[6] = (tag >> 8) & 0xFF; // tag
        b[7]  = msize & 0xFF; b[8]  = (msize >> 8) & 0xFF;
        b[9]  = (msize >> 16) & 0xFF; b[10] = (msize >> 24) & 0xFF;
        b[11] = 6; b[12] = 0;                         // strlen = 6
        msg.replace(13, 6, "9P2000");

        RMessage r;
        QString err;
        QVERIFY(decodeR(msg, r, &err));
        QCOMPARE(r.type, MsgType::Rversion);
        QCOMPARE(r.tag, tag);
        QCOMPARE(r.msize, msize);
        QCOMPARE(r.version, ver);
    }

    // ── Round-trip: Rerror ──────────────────────────────────────────────────
    void rerror_roundTrip()
    {
        const QString ename = QStringLiteral("no permission");
        const uint16_t tag = 2;
        // size=4+1+2+2+13=22
        const int total = 4 + 1 + 2 + 2 + 13;
        QByteArray msg(total, 0);
        auto* b = reinterpret_cast<uint8_t*>(msg.data());
        b[0] = uint8_t(total); b[1]=0; b[2]=0; b[3]=0;
        b[4] = 107;                                    // Rerror
        b[5] = tag & 0xFF; b[6] = 0;
        b[7] = 13; b[8] = 0;                           // strlen
        msg.replace(9, 13, "no permission");

        RMessage r;
        QVERIFY(decodeR(msg, r));
        QCOMPARE(r.type, MsgType::Rerror);
        QCOMPARE(r.ename, ename);
    }

    // ── Round-trip: Rwalk (2 qids) ──────────────────────────────────────────
    void rwalk_roundTrip()
    {
        // Rwalk: size[4] type[1] tag[2] nwqid[2] then nwqid×qid[13]
        // total = 4+1+2+2+2*13 = 35
        const uint16_t tag = 3;
        const int total = 4 + 1 + 2 + 2 + 2 * 13;
        QByteArray msg(total, 0);
        auto* b = reinterpret_cast<uint8_t*>(msg.data());
        b[0] = uint8_t(total); b[1]=0; b[2]=0; b[3]=0;
        b[4] = 111;                                    // Rwalk
        b[5] = tag & 0xFF; b[6] = 0;
        b[7] = 2; b[8] = 0;                            // nwqid = 2
        // qid 0: type=0x00, version=1, path=100
        b[9]  = 0x00;
        b[10]=1; b[11]=0; b[12]=0; b[13]=0;
        b[14]=100; b[15]=0; b[16]=0; b[17]=0; b[18]=0; b[19]=0; b[20]=0; b[21]=0;
        // qid 1: type=0x80, version=2, path=200
        b[22] = 0x80;
        b[23]=2; b[24]=0; b[25]=0; b[26]=0;
        b[27]=200; b[28]=0; b[29]=0; b[30]=0; b[31]=0; b[32]=0; b[33]=0; b[34]=0;

        RMessage r;
        QVERIFY(decodeR(msg, r));
        QCOMPARE(r.type, MsgType::Rwalk);
        QCOMPARE(r.wqids.size(), 2);
        QCOMPARE(r.wqids[0].type, uint8_t(0x00));
        QCOMPARE(r.wqids[0].version, uint32_t(1));
        QCOMPARE(r.wqids[0].path, uint64_t(100));
        QCOMPARE(r.wqids[1].type, uint8_t(0x80));
        QCOMPARE(r.wqids[1].version, uint32_t(2));
        QCOMPARE(r.wqids[1].path, uint64_t(200));
    }

    // ── Round-trip: Ropen ───────────────────────────────────────────────────
    void ropen_roundTrip()
    {
        // Ropen: size[4] type[1] tag[2] qid[13] iounit[4]
        // total = 4+1+2+13+4 = 24
        const uint16_t tag = 4;
        const int total = 24;
        QByteArray msg(total, 0);
        auto* b = reinterpret_cast<uint8_t*>(msg.data());
        b[0] = uint8_t(total); b[1]=0; b[2]=0; b[3]=0;
        b[4] = 113;                                    // Ropen
        b[5] = tag & 0xFF; b[6] = 0;
        // qid: type=0, version=0, path=42
        b[7] = 0; b[8]=0; b[9]=0; b[10]=0; b[11]=0;
        b[12]=42; b[13]=0; b[14]=0; b[15]=0; b[16]=0; b[17]=0; b[18]=0; b[19]=0;
        // iounit = 512
        b[20]=0x00; b[21]=0x02; b[22]=0; b[23]=0;

        RMessage r;
        QVERIFY(decodeR(msg, r));
        QCOMPARE(r.type, MsgType::Ropen);
        QCOMPARE(r.qid.path, uint64_t(42));
        QCOMPARE(r.iounit, uint32_t(512));
    }

    // ── Round-trip: Rread ───────────────────────────────────────────────────
    void rread_roundTrip()
    {
        // Rread: size[4] type[1] tag[2] count[4] data[count]
        const uint16_t tag = 5;
        const QByteArray payload = QByteArray("hello");
        const uint32_t count = uint32_t(payload.size()); // 5
        const int total = 4 + 1 + 2 + 4 + int(count);   // = 16
        QByteArray msg(total, 0);
        auto* b = reinterpret_cast<uint8_t*>(msg.data());
        b[0] = uint8_t(total); b[1]=0; b[2]=0; b[3]=0;
        b[4] = 117;                                    // Rread
        b[5] = tag & 0xFF; b[6] = 0;
        b[7] = count & 0xFF; b[8]=(count>>8)&0xFF;
        b[9] = (count>>16)&0xFF; b[10]=(count>>24)&0xFF;
        msg.replace(11, int(count), payload);

        RMessage r;
        QVERIFY(decodeR(msg, r));
        QCOMPARE(r.type, MsgType::Rread);
        QCOMPARE(r.count, count);
        QCOMPARE(r.data, payload);
    }

    // ── Round-trip: Rwrite ──────────────────────────────────────────────────
    void rwrite_roundTrip()
    {
        // Rwrite: size[4] type[1] tag[2] count[4]
        // total = 4+1+2+4 = 11
        const uint16_t tag = 6;
        const uint32_t count = 1024;
        const int total = 11;
        QByteArray msg(total, 0);
        auto* b = reinterpret_cast<uint8_t*>(msg.data());
        b[0] = uint8_t(total); b[1]=0; b[2]=0; b[3]=0;
        b[4] = 119;                                    // Rwrite
        b[5] = tag & 0xFF; b[6] = 0;
        b[7] = count & 0xFF; b[8]=(count>>8)&0xFF;
        b[9] = (count>>16)&0xFF; b[10]=(count>>24)&0xFF;

        RMessage r;
        QVERIFY(decodeR(msg, r));
        QCOMPARE(r.type, MsgType::Rwrite);
        QCOMPARE(r.count, count);
    }

    // ── Round-trip: Rclunk ──────────────────────────────────────────────────
    void rclunk_roundTrip()
    {
        // Rclunk: size[4] type[1] tag[2]  (empty body)
        // total = 7
        const uint16_t tag = 7;
        const int total = 7;
        QByteArray msg(total, 0);
        auto* b = reinterpret_cast<uint8_t*>(msg.data());
        b[0] = uint8_t(total); b[1]=0; b[2]=0; b[3]=0;
        b[4] = 121;                                    // Rclunk
        b[5] = tag & 0xFF; b[6] = 0;

        RMessage r;
        QVERIFY(decodeR(msg, r));
        QCOMPARE(r.type, MsgType::Rclunk);
        QCOMPARE(r.tag, tag);
    }

    // ── frameLength ─────────────────────────────────────────────────────────
    void frameLength_returnsSize()
    {
        QByteArray buf(10, 0);
        auto* b = reinterpret_cast<uint8_t*>(buf.data());
        b[0] = 29; b[1] = 0; b[2] = 0; b[3] = 0;   // size = 29
        QCOMPARE(frameLength(buf), 29);
    }

    void frameLength_shortBuffer()
    {
        QByteArray buf(3, 0); // fewer than 4 bytes
        QCOMPARE(frameLength(buf), -1);
    }

    // ── Twrite layout ────────────────────────────────────────────────────────
    void twrite_layout()
    {
        // encodeTwrite(tag, fid, offset, data)
        // body: fid[4] offset[8] count[4] data[count]
        // header: size[4] type[1] tag[2]
        // total = 4+1+2 + 4+8+4+5 = 28
        const uint16_t tag = 8;
        const uint32_t fid = 3;
        const uint64_t offset = 0;
        const QByteArray data = QByteArray("\x01\x02\x03\x04\x05", 5);
        const int expectedTotal = 4 + 1 + 2 + 4 + 8 + 4 + 5; // = 28

        QByteArray msg = encodeTwrite(tag, fid, offset, data);
        QCOMPARE(msg.size(), expectedTotal);

        const auto* b = reinterpret_cast<const uint8_t*>(msg.constData());

        // size field
        uint32_t sz = uint32_t(b[0]) | (uint32_t(b[1])<<8)
                    | (uint32_t(b[2])<<16) | (uint32_t(b[3])<<24);
        QCOMPARE(sz, uint32_t(expectedTotal));

        // type = Twrite (118)
        QCOMPARE(b[4], uint8_t(118));

        // tag
        uint16_t t = uint16_t(b[5]) | (uint16_t(b[6])<<8);
        QCOMPARE(t, tag);

        // fid
        uint32_t f = uint32_t(b[7]) | (uint32_t(b[8])<<8)
                   | (uint32_t(b[9])<<16) | (uint32_t(b[10])<<24);
        QCOMPARE(f, fid);

        // offset (8 bytes) = 0
        for (int i = 11; i < 19; ++i)
            QCOMPARE(b[i], uint8_t(0));

        // count = 5
        uint32_t cnt = uint32_t(b[19]) | (uint32_t(b[20])<<8)
                     | (uint32_t(b[21])<<16) | (uint32_t(b[22])<<24);
        QCOMPARE(cnt, uint32_t(5));

        // data bytes
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(b+23), 5), data);
    }

    // ── decodeR rejects short/malformed ─────────────────────────────────────
    void decodeR_rejectsShort()
    {
        QByteArray buf(3, 0);
        RMessage r;
        QString err;
        QVERIFY(!decodeR(buf, r, &err));
        QVERIFY(!err.isEmpty());
    }

    void decodeR_rejectsSizeMismatch()
    {
        // size field says 29 but buffer is only 7 bytes
        QByteArray buf(7, 0);
        auto* b = reinterpret_cast<uint8_t*>(buf.data());
        b[0] = 29; b[1]=0; b[2]=0; b[3]=0;
        b[4] = 101; b[5]=0; b[6]=0;
        RMessage r;
        QVERIFY(!decodeR(buf, r));
    }
};

QTEST_MAIN(TestStyx9p)
#include "test_styx9p.moc"
