#pragma once
#include <cstdint>
#include <cstddef>

namespace studio {

enum class State { Idle, Streaming, Recording };

struct Metrics {
    double   sps           = 0.0;
    uint64_t droppedSamples = 0;
    size_t   bufferFill    = 0;
};

} // namespace studio
