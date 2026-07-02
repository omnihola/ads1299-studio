#include "Ads1299WordParser.h"

namespace studio::mmb0 {

Ads1299WordParser::Ads1299WordParser(WordOrder order)
    : order_(order)
{}

uint32_t Ads1299WordParser::readWord(const uint8_t* p) const
{
    if (order_ == WordOrder::LittleEndian) {
        return uint32_t(p[0])
             | (uint32_t(p[1]) << 8)
             | (uint32_t(p[2]) << 16)
             | (uint32_t(p[3]) << 24);
    }
    return uint32_t(p[3])
         | (uint32_t(p[2]) << 8)
         | (uint32_t(p[1]) << 16)
         | (uint32_t(p[0]) << 24);
}

void Ads1299WordParser::feed(const QByteArray& bytes)
{
    buf_.append(bytes);

    // Consume whole samples via a running offset; compact the buffer once at
    // the end (a remove() per sample would memmove the tail repeatedly).
    const auto* raw = reinterpret_cast<const uint8_t*>(buf_.constData());
    int offset = 0;

    while (buf_.size() - offset >= kBytesPerSample) {
        const uint8_t* rec = raw + offset;

        studio::EegFrame frame;
        frame.seq = seq_++;

        // Word 0: STATUS = [1100][LOFF_STATP:8][LOFF_STATN:8][GPIO:4]
        const uint32_t status = readWord(rec) & 0xFFFFFFu;
        frame.statP = uint8_t((status >> 12) & 0xFF);
        frame.statN = uint8_t((status >>  4) & 0xFF);
        frame.gpio  = uint8_t( status        & 0x0F);

        // Words 1..8: channel values — the low 24 bits of each word, decoded
        // with the shared 24-bit two's-complement helper.
        for (int c = 0; c < 8; ++c) {
            const uint32_t w = readWord(rec + 4 * (c + 1));
            frame.ch[c] = studio::int24ToInt32(uint8_t(w >> 16),
                                               uint8_t(w >> 8),
                                               uint8_t(w));
        }

        out_.append(frame);
        offset += kBytesPerSample;
    }

    if (offset > 0)
        buf_.remove(0, offset);
}

QVector<studio::EegFrame> Ads1299WordParser::takeFrames()
{
    QVector<studio::EegFrame> result;
    result.swap(out_);
    return result;
}

void Ads1299WordParser::reset()
{
    buf_.clear();
    out_.clear();
    seq_ = 0;
}

void Ads1299WordParser::dropPartialSample()
{
    buf_.clear();
}

int Ads1299WordParser::bufferedBytes() const
{
    return buf_.size();
}

uint32_t Ads1299WordParser::seq() const
{
    return seq_;
}

} // namespace studio::mmb0
