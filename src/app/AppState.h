#pragma once
#include <cstdint>
#include <cstddef>

namespace studio {

enum class State { Idle, Streaming, Recording };

/// Which kind of acquisition source is currently active.
/// The simulator is built-in and always usable; hardware sources only become
/// active through a successful connect (SessionController::connectSerial /
/// connectMmb0).
enum class SourceType { Simulated, Serial, Mmb0 };

struct Metrics {
    double   sps           = 0.0;
    uint64_t droppedSamples = 0;
    size_t   bufferFill    = 0;
    uint8_t  leadOffP      = 0;   // LOFF_STATP from last frame — bit c set = ch c electrode off (P side)
    uint8_t  leadOffN      = 0;   // LOFF_STATN from last frame — bit c set = ch c electrode off (N side)
};

} // namespace studio
