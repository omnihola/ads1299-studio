#pragma once

#include <QByteArray>
#include <QVector>
#include <cstdint>
#include "core/acquisition/EegFrame.h"

namespace studio::mmb0 {

// Parses MMB0 /ads1299evm/data stream (ADS1299 RDATAC records) into EegFrame.
//
// Record format: 27 bytes = status[3] + ch1[3] … ch8[3]
// All values are 24-bit, big-endian (MSB first), two's-complement.
//
// STATUS word layout (MSB→LSB):
//   [4 bits = 1100] [LOFF_STATP: 8 bits] [LOFF_STATN: 8 bits] [GPIO: 4 bits]
class Ads1299RecordParser {
public:
    static constexpr int kRecordBytes = 27;

    // Pure: parse exactly one 27-byte record at rec into an EegFrame with the
    // given seq number. Does not modify any internal state.
    static studio::EegFrame parseRecord(const uint8_t* rec, uint32_t seq);

    // Streaming: accumulate bytes; every 27 bytes → one EegFrame
    // (seq auto-increments). Leftover partial bytes are retained in the
    // internal buffer and will be consumed by future feed() calls.
    void feed(const QByteArray& bytes);

    // Returns all frames accumulated since the last call, then clears the queue.
    QVector<studio::EegFrame> takeFrames();

    // Clears the internal buffer and resets seq to 0.
    void reset();

    // Returns the next sequence number that will be assigned.
    uint32_t seq() const;

private:
    QByteArray               buf_;
    QVector<studio::EegFrame> out_;
    uint32_t                 seq_ = 0;
};

} // namespace studio::mmb0
