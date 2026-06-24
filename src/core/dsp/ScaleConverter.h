#pragma once
#include <cstdint>

namespace studio {
class ScaleConverter {
public:
    // Default: gain=1 (no amplification). Replaced on open() by per-channel gain.
    ScaleConverter() : ScaleConverter(1) {}
    explicit ScaleConverter(int gain);
    double countsToMicrovolts(int32_t counts) const;
    double lsbMicrovolts() const { return lsbUv_; }

private:
    double lsbUv_;
};
}
