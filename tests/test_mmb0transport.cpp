#include <QtTest/QtTest>
#include "core/acquisition/mmb0/MessageDeframer.h"
#include "core/acquisition/mmb0/Mmb0UsbTransport.h"

using namespace studio::styx;
using namespace studio::mmb0;

// Helper: build a minimal size-prefixed 9P frame of exactly `totalLen` bytes.
// First 4 bytes = totalLen in little-endian; rest filled with 0xAB.
static QByteArray makeFrame(int totalLen)
{
    QByteArray frame(totalLen, char(0xAB));
    auto* b = reinterpret_cast<uint8_t*>(frame.data());
    b[0] = uint8_t(totalLen & 0xFF);
    b[1] = uint8_t((totalLen >> 8) & 0xFF);
    b[2] = uint8_t((totalLen >> 16) & 0xFF);
    b[3] = uint8_t((totalLen >> 24) & 0xFF);
    return frame;
}

class TestMmb0Transport : public QObject
{
    Q_OBJECT

private slots:

    // ── Deframer: single complete message ────────────────────────────────────
    void deframerSingleMessage()
    {
        MessageDeframer df;
        QByteArray frame = makeFrame(10);

        df.feed(frame);

        QByteArray msg;
        QVERIFY(df.take(msg));
        QCOMPARE(msg.size(), 10);
        QCOMPARE(msg, frame);

        // Second take must return false — buffer is empty.
        QByteArray msg2;
        QVERIFY(!df.take(msg2));
    }

    // ── Deframer: split delivery ─────────────────────────────────────────────
    void deframerSplit()
    {
        MessageDeframer df;
        QByteArray frame = makeFrame(10);

        // Feed first 6 bytes — message not yet complete.
        df.feed(frame.left(6));
        QByteArray msg;
        QVERIFY(!df.take(msg));

        // Feed remaining 4 bytes — now complete.
        df.feed(frame.mid(6));
        QVERIFY(df.take(msg));
        QCOMPARE(msg.size(), 10);
        QCOMPARE(msg, frame);
    }

    // ── Deframer: two concatenated messages in one feed ──────────────────────
    void deframerMultiple()
    {
        MessageDeframer df;
        QByteArray frame1 = makeFrame(8);
        QByteArray frame2 = makeFrame(12);

        df.feed(frame1 + frame2);

        QByteArray msg1, msg2, msg3;
        QVERIFY(df.take(msg1));
        QCOMPARE(msg1.size(), 8);
        QCOMPARE(msg1, frame1);

        QVERIFY(df.take(msg2));
        QCOMPARE(msg2.size(), 12);
        QCOMPARE(msg2, frame2);

        QVERIFY(!df.take(msg3));
    }

    // ── Deframer: fewer than 4 bytes — no crash, no spurious message ────────
    void deframerPartialHeader()
    {
        MessageDeframer df;
        // Only 2 bytes — can't even read the size field.
        df.feed(QByteArray(2, char(0x05)));
        QByteArray msg;
        QVERIFY(!df.take(msg));
        QCOMPARE(df.buffered(), 2);
    }

    // ── Deframer: oversized frame poison detection and resync ──────────────────
    void deframerOversizedFrameResync()
    {
        MessageDeframer df;

        // Craft a 4-byte buffer where the size field decodes to a huge value > kMaxFrameLen.
        // size[4] = 0x40000001 (1 GiB + 1 byte) — well beyond the 1 MiB ceiling.
        QByteArray poisoned(4, char(0x00));
        auto* b = reinterpret_cast<uint8_t*>(poisoned.data());
        b[0] = 0x01;  // little-endian low byte
        b[1] = 0x00;
        b[2] = 0x00;
        b[3] = 0x40;  // high byte: 0x40000001 in LE

        df.feed(poisoned);

        // take() should detect oversized frame, clear the buffer, and return false.
        QByteArray msg;
        QVERIFY(!df.take(msg));
        QCOMPARE(df.buffered(), 0);  // Buffer was cleared.

        // Now feed a valid message and verify the stream has resynced.
        QByteArray validFrame = makeFrame(8);
        df.feed(validFrame);

        // take() should succeed and return the valid message.
        QVERIFY(df.take(msg));
        QCOMPARE(msg.size(), 8);
        QCOMPARE(msg, validFrame);
    }

    // ── send() wire framing ──────────────────────────────────────────────────
    // The verified reference client (mmb0_acquire.c styx_tx) sends EXACTLY the
    // 9P message bytes — no zero-padding — and terminates transfers that are an
    // exact multiple of the 64-byte packet size with a zero-length packet (ZLP)
    // so the device sees the end of the transfer. Zero-padding instead would
    // inject garbage bytes that the estyx deframer reads as the next message's
    // size field.
    void zlpNeededOnlyOnPacketMultiples()
    {
        QVERIFY(!Mmb0UsbTransport::needsZlp(1));
        QVERIFY(!Mmb0UsbTransport::needsZlp(63));
        QVERIFY( Mmb0UsbTransport::needsZlp(64));
        QVERIFY(!Mmb0UsbTransport::needsZlp(65));
        QVERIFY( Mmb0UsbTransport::needsZlp(128));
        QVERIFY(!Mmb0UsbTransport::needsZlp(0));   // nothing sent → no ZLP
    }

    // ── Device enumeration: must not crash ───────────────────────────────────
    void enumerationDoesNotCrash()
    {
        bool result = Mmb0UsbTransport::isDevicePresent();
        // Just verify it returned a bool without crashing.
        QVERIFY(result == true || result == false);
    }

    // ── Open/close: skip if no device connected ──────────────────────────────
    void openIfPresent()
    {
        if (!Mmb0UsbTransport::isDevicePresent())
            QSKIP("No MMB0 device connected — skipping open/close test");

        Mmb0UsbTransport t;
        QVERIFY(t.open());
        QVERIFY(t.isOpen());
        t.close();
        QVERIFY(!t.isOpen());
    }
};

QTEST_MAIN(TestMmb0Transport)
#include "test_mmb0transport.moc"
