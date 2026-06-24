#include "Ads1299RecordParser.h"

namespace studio::mmb0 {

// ── parseRecord (static) ──────────────────────────────────────────────────────
studio::EegFrame Ads1299RecordParser::parseRecord(const uint8_t* rec, uint32_t seq)
{
    studio::EegFrame frame;
    frame.seq = seq;

    // Decode 24-bit STATUS word (big-endian)
    const uint32_t statusWord = (uint32_t(rec[0]) << 16)
                              | (uint32_t(rec[1]) <<  8)
                              |  uint32_t(rec[2]);

    // STATUS layout: [1100][LOFF_STATP:8][LOFF_STATN:8][GPIO:4]
    frame.statP = static_cast<uint8_t>((statusWord >> 12) & 0xFF);
    frame.statN = static_cast<uint8_t>((statusWord >>  4) & 0xFF);
    frame.gpio  = static_cast<uint8_t>( statusWord        & 0x0F);

    // Decode 8 channel values (big-endian 24-bit two's complement)
    for (int c = 0; c < 8; ++c) {
        const int base = 3 + 3 * c;
        frame.ch[c] = studio::int24ToInt32(rec[base], rec[base + 1], rec[base + 2]);
    }

    return frame;
}

// ── feed ─────────────────────────────────────────────────────────────────────
void Ads1299RecordParser::feed(const QByteArray& bytes)
{
    buf_.append(bytes);

    while (buf_.size() >= kRecordBytes) {
        const auto* raw = reinterpret_cast<const uint8_t*>(buf_.constData());
        out_.append(parseRecord(raw, seq_++));
        buf_.remove(0, kRecordBytes);
    }
}

// ── takeFrames ────────────────────────────────────────────────────────────────
QVector<studio::EegFrame> Ads1299RecordParser::takeFrames()
{
    QVector<studio::EegFrame> result;
    result.swap(out_);
    return result;
}

// ── reset ────────────────────────────────────────────────────────────────────
void Ads1299RecordParser::reset()
{
    buf_.clear();
    out_.clear();
    seq_ = 0;
}

// ── seq ──────────────────────────────────────────────────────────────────────
uint32_t Ads1299RecordParser::seq() const
{
    return seq_;
}

} // namespace studio::mmb0
