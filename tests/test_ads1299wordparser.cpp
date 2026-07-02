// tests/test_ads1299wordparser.cpp
// TDD tests for Ads1299WordParser — the MMB0 /ads1299evm/data decoder.
//
// VERIFIED firmware facts (see docs/2026-07-01-mmb0-ads1299-reverse-engineering
// -and-connection-ui.md and the PDK firmware sources):
//   - The firmware queue stores 32-bit LONGS, not packed 27-byte RDATAC records
//     (acquire.cpp: acquire_data_len() returns queue.len()<<2 bytes).
//   - t1299_ob.c ADS1299_readblock(): "This buffer must be a multiple of the
//     nine" — each EEG sample occupies 9 words: status + ch1..ch8, one 24-bit
//     value per 32-bit word (McBSP word length set to 24 bits before the DMA).
//   - Default blocksize_samples = 576 words = 64 samples × 9 words.
//
// The on-wire byte order of each 32-bit word still needs one raw hardware dump
// to confirm (internal test signal); the parser therefore takes a WordOrder
// parameter. Little-endian is the default (matches every other 9P wire field).

#include <QtTest>
#include <QByteArray>
#include <cstdint>

#include "core/acquisition/mmb0/Ads1299WordParser.h"
#include "core/acquisition/EegFrame.h"

using studio::mmb0::Ads1299WordParser;

namespace {

// Append one 32-bit word in the given order.
void putWord(QByteArray& b, uint32_t w, Ads1299WordParser::WordOrder order)
{
    if (order == Ads1299WordParser::WordOrder::LittleEndian) {
        b.append(char(w & 0xFF));
        b.append(char((w >> 8) & 0xFF));
        b.append(char((w >> 16) & 0xFF));
        b.append(char((w >> 24) & 0xFF));
    } else {
        b.append(char((w >> 24) & 0xFF));
        b.append(char((w >> 16) & 0xFF));
        b.append(char((w >> 8) & 0xFF));
        b.append(char(w & 0xFF));
    }
}

// Build one 9-word sample: status word + 8 channel words.
// Channel values are 24-bit two's complement placed in the low 24 bits.
QByteArray makeSample(uint32_t status, const int32_t ch[8],
                      Ads1299WordParser::WordOrder order)
{
    QByteArray b;
    putWord(b, status & 0xFFFFFFu, order);
    for (int c = 0; c < 8; ++c)
        putWord(b, uint32_t(ch[c]) & 0xFFFFFFu, order);
    return b;
}

} // namespace

class TestAds1299WordParser : public QObject {
    Q_OBJECT

private slots:

    // One full 9-word sample decodes to one EegFrame with correct channels,
    // including sign extension of negative 24-bit values.
    void decodesOneSample()
    {
        Ads1299WordParser p;   // default LittleEndian
        // STATUS 24-bit layout: [1100][LOFF_STATP:8][LOFF_STATN:8][GPIO:4]
        const uint32_t status = (0xCu << 20) | (0xA5u << 12) | (0x3Cu << 4) | 0x7u;
        const int32_t ch[8] = { 0, 1, -1, 8388607, -8388608, 4660, -30875, 42 };

        p.feed(makeSample(status, ch, Ads1299WordParser::WordOrder::LittleEndian));
        const auto frames = p.takeFrames();

        QCOMPARE(frames.size(), qsizetype(1));
        const auto& f = frames[0];
        QCOMPARE(f.statP, uint8_t(0xA5));
        QCOMPARE(f.statN, uint8_t(0x3C));
        QCOMPARE(f.gpio,  uint8_t(0x7));
        for (int c = 0; c < 8; ++c)
            QCOMPARE(f.ch[c], ch[c]);
    }

    // Bytes may arrive split anywhere — partial words/samples are retained.
    void retainsPartialAcrossFeeds()
    {
        Ads1299WordParser p;
        const int32_t ch[8] = { 10, 11, 12, 13, 14, 15, 16, 17 };
        QByteArray sample = makeSample(0xC00000u, ch,
                                       Ads1299WordParser::WordOrder::LittleEndian);
        QCOMPARE(sample.size(), 36);

        p.feed(sample.left(5));               // less than one word
        QCOMPARE(p.takeFrames().size(), qsizetype(0));

        p.feed(sample.mid(5, 20));            // still short of 36
        QCOMPARE(p.takeFrames().size(), qsizetype(0));

        p.feed(sample.mid(25));               // completes the sample
        const auto frames = p.takeFrames();
        QCOMPARE(frames.size(), qsizetype(1));
        for (int c = 0; c < 8; ++c)
            QCOMPARE(frames[0].ch[c], ch[c]);
    }

    // Sequence numbers auto-increment across feeds; reset() zeroes them.
    void seqIncrementsAndResets()
    {
        Ads1299WordParser p;
        const int32_t ch[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        QByteArray two;
        two += makeSample(0xC00000u, ch, Ads1299WordParser::WordOrder::LittleEndian);
        two += makeSample(0xC00000u, ch, Ads1299WordParser::WordOrder::LittleEndian);

        p.feed(two);
        auto frames = p.takeFrames();
        QCOMPARE(frames.size(), qsizetype(2));
        QCOMPARE(frames[0].seq, uint32_t(0));
        QCOMPARE(frames[1].seq, uint32_t(1));

        p.reset();
        QCOMPARE(p.seq(), uint32_t(0));
        p.feed(makeSample(0xC00000u, ch, Ads1299WordParser::WordOrder::LittleEndian));
        frames = p.takeFrames();
        QCOMPARE(frames.size(), qsizetype(1));
        QCOMPARE(frames[0].seq, uint32_t(0));
    }

    // Big-endian mode decodes the same values from byte-swapped words.
    void bigEndianModeDecodes()
    {
        Ads1299WordParser p(Ads1299WordParser::WordOrder::BigEndian);
        const int32_t ch[8] = { -2, 3, -400000, 5, 6, 7, 8, 9 };
        p.feed(makeSample(0xC00000u, ch, Ads1299WordParser::WordOrder::BigEndian));
        const auto frames = p.takeFrames();
        QCOMPARE(frames.size(), qsizetype(1));
        for (int c = 0; c < 8; ++c)
            QCOMPARE(frames[0].ch[c], ch[c]);
    }

    // Words per sample constant matches the firmware contract.
    void sampleGeometryConstants()
    {
        QCOMPARE(Ads1299WordParser::kWordsPerSample, 9);
        QCOMPARE(Ads1299WordParser::kBytesPerSample, 36);
    }
};

QTEST_MAIN(TestAds1299WordParser)
#include "test_ads1299wordparser.moc"
