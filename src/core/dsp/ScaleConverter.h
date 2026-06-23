#pragma once
#include <cstdint>

namespace studio {
class ScaleConverter {
public:
    explicit ScaleConverter(int gain);
    double countsToMicrovolts(int32_t counts) const;
    double lsbMicrovolts() const { return lsbUv_; }

private:
    double lsbUv_;
};
}
