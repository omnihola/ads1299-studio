#pragma once

#include <cstdint>
#include <vector>

namespace studio {
// Big-endian 24-bit two's complement (native ADS1299 DATA order) -> int32
inline int32_t int24ToInt32(uint8_t hi, uint8_t mid, uint8_t lo) {
    int32_t v = (int32_t(hi) << 16) | (int32_t(mid) << 8) | int32_t(lo);
    if (v & 0x800000) v |= ~0xFFFFFF; // sign-extend
    return v;
}

struct EegFrame {
    uint32_t seq = 0;
    int32_t  ch[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t  statP = 0, statN = 0, gpio = 0;
};

using EegFrameBatch = std::vector<EegFrame>;
}
