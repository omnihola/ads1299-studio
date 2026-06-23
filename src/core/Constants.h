#pragma once

namespace studio {
inline constexpr int    kNumChannels  = 8;
inline constexpr double kVref         = 4.5;   // ADS1299 internal reference (volts)
inline constexpr int    kAdcFullScale = 1 << 23; // 2^23 for 24-bit signed
}
