#include "core/dsp/ScaleConverter.h"
#include "core/Constants.h"

namespace studio {
ScaleConverter::ScaleConverter(int gain)
    : lsbUv_((kVref / (double(gain) * kAdcFullScale)) * 1e6) {}

double ScaleConverter::countsToMicrovolts(int32_t counts) const {
    return counts * lsbUv_;
}
}
