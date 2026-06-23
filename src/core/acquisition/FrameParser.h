#pragma once

#include <QByteArray>
#include <QVector>
#include <cstdint>
#include "core/acquisition/EegFrame.h"

namespace studio {

class FrameParser
{
public:
    static constexpr int kFrameSize = 34;

    // Accumulate bytes; may contain partial / multiple / garbage data.
    void feed(const QByteArray& bytes);

    // Returns all decoded frames since the last call, then clears the queue.
    QVector<EegFrame> takeFrames();

    uint64_t crcErrors() const { return m_crcErrors; }
    uint64_t resyncs()   const { return m_resyncs; }

    // CRC-8/SMBUS: poly 0x07, init 0x00, no reflection, no final XOR.
    static uint8_t crc8(const uint8_t* data, int len);

private:
    QByteArray       m_buf;
    QVector<EegFrame> m_queue;
    uint64_t         m_crcErrors = 0;
    uint64_t         m_resyncs   = 0;

    static constexpr uint8_t kSync0 = 0xA5;
    static constexpr uint8_t kSync1 = 0x5A;
};

} // namespace studio
