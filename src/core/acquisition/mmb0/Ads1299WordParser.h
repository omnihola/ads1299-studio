#pragma once

#include <QByteArray>
#include <QVector>
#include <cstdint>
#include "core/acquisition/EegFrame.h"

namespace studio::mmb0 {

// Parses the MMB0 /ads1299evm/data stream into EegFrame.
//
// VERIFIED firmware layout (PDK sources, see the 2026-07-01 reverse-engineering
// handoff): the firmware block queue stores 32-BIT WORDS, not packed 27-byte
// RDATAC records. t1299_ob.c ADS1299_readblock() requires the buffer to be "a
// multiple of the nine": each EEG sample occupies 9 words — STATUS + CH1..CH8,
// one 24-bit value per 32-bit word (McBSP word length is set to 24 bits before
// the DMA transfer). blocksize_samples counts WORDS; its default 576 = 64
// samples x 9 words.
//
// The byte order of each 32-bit word on the USB wire is the one remaining
// unknown (it must be confirmed with a raw dump of the internal test signal);
// WordOrder makes that a one-line switch. Little-endian is the default, matching
// every other 9P wire field.
//
// STATUS 24-bit layout (MSB->LSB): [1100][LOFF_STATP:8][LOFF_STATN:8][GPIO:4]
class Ads1299WordParser {
public:
    enum class WordOrder { LittleEndian, BigEndian };

    static constexpr int kWordsPerSample = 9;    // status + 8 channels
    static constexpr int kBytesPerSample = kWordsPerSample * 4;

    explicit Ads1299WordParser(WordOrder order = WordOrder::LittleEndian);

    // Streaming: accumulate bytes; every 36 bytes -> one EegFrame
    // (seq auto-increments). Leftover partial bytes are retained.
    void feed(const QByteArray& bytes);

    // Returns all frames accumulated since the last call, then clears the queue.
    QVector<studio::EegFrame> takeFrames();

    // Clears the internal buffer and resets seq to 0.
    void reset();

    // Next sequence number that will be assigned.
    uint32_t seq() const;

private:
    uint32_t readWord(const uint8_t* p) const;

    WordOrder                  order_;
    QByteArray                 buf_;
    QVector<studio::EegFrame>  out_;
    uint32_t                   seq_ = 0;
};

} // namespace studio::mmb0
