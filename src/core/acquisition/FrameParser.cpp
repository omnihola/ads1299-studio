#include "core/acquisition/FrameParser.h"
#include <cstring>

namespace studio {

// ---------------------------------------------------------------------------
// CRC-8/SMBUS: poly 0x07, init 0x00, MSB-first, no reflection, no final XOR
// ---------------------------------------------------------------------------
uint8_t FrameParser::crc8(const uint8_t* data, int len)
{
    uint8_t crc = 0x00;
    for (int i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07)
                               : static_cast<uint8_t>(crc << 1);
    }
    return crc;
}

// ---------------------------------------------------------------------------
// feed / takeFrames
// ---------------------------------------------------------------------------
void FrameParser::feed(const QByteArray& bytes)
{
    m_buf.append(bytes);

    while (true) {
        // --- Step 1: find sync word 0xA5 0x5A at the front ---
        if (m_buf.size() < 2) {
            // Keep whatever is there; can't form a sync yet.
            break;
        }

        const auto* buf = reinterpret_cast<const uint8_t*>(m_buf.constData());

        if (buf[0] != kSync0 || buf[1] != kSync1) {
            // Scan forward to find the next possible sync.
            int syncPos = -1;
            for (int i = 1; i < m_buf.size() - 1; ++i) {
                if (buf[i] == kSync0 && buf[i + 1] == kSync1) {
                    syncPos = i;
                    break;
                }
            }

            if (syncPos == -1) {
                // No sync found. Keep the last byte (could be lone 0xA5).
                if (m_buf.size() >= 1) {
                    ++m_resyncs;
                    m_buf = m_buf.right(1);
                }
                break;
            }

            // Discard bytes up to syncPos — counts as one resync event.
            ++m_resyncs;
            m_buf = m_buf.mid(syncPos);
            // Re-evaluate from the new front.
            continue;
        }

        // --- Step 2: we have sync at the front; need full frame ---
        if (m_buf.size() < kFrameSize)
            break;  // wait for more data

        // --- Step 3: verify CRC ---
        uint8_t computed = crc8(buf + 2, 31);           // bytes [2..32]
        uint8_t expected = buf[33];

        if (computed != expected) {
            // CRC failure: discard only the first sync byte, rescan.
            ++m_crcErrors;
            m_buf = m_buf.mid(1);
            continue;
        }

        // --- Step 4: decode frame ---
        EegFrame frame;

        // seq: little-endian uint32 from bytes 2..5
        frame.seq = (uint32_t(buf[2]))
                  | (uint32_t(buf[3]) << 8)
                  | (uint32_t(buf[4]) << 16)
                  | (uint32_t(buf[5]) << 24);

        // 8 channels: big-endian int24 at bytes 6..29
        for (int c = 0; c < 8; ++c) {
            frame.ch[c] = int24ToInt32(buf[6 + c * 3],
                                       buf[7 + c * 3],
                                       buf[8 + c * 3]);
        }

        frame.statP = buf[30];
        frame.statN = buf[31];
        frame.gpio  = buf[32];

        m_queue.append(frame);
        m_buf = m_buf.mid(kFrameSize);
    }
}

QVector<EegFrame> FrameParser::takeFrames()
{
    QVector<EegFrame> result;
    result.swap(m_queue);
    return result;
}

} // namespace studio
