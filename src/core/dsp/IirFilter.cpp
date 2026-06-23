#include "IirFilter.h"

#include <cmath>

namespace studio {

// ---------------------------------------------------------------------------
// Internal helper — compute RBJ common intermediate values
// ---------------------------------------------------------------------------
namespace {

struct RbjCommon {
    double w0;
    double cosw;
    double sinw;
    double alpha;
};

RbjCommon rbj(double fs, double f0, double Q) noexcept
{
    RbjCommon c{};
    c.w0    = 2.0 * M_PI * f0 / fs;
    c.cosw  = std::cos(c.w0);
    c.sinw  = std::sin(c.w0);
    c.alpha = c.sinw / (2.0 * Q);
    return c;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// IirFilter::notch
// ---------------------------------------------------------------------------
IirFilter IirFilter::notch(double fs, double f0, double Q)
{
    auto c = rbj(fs, f0, Q);

    const double a0 = 1.0 + c.alpha;
    Biquad bq;
    bq.b0 =  1.0           / a0;
    bq.b1 = -2.0 * c.cosw  / a0;
    bq.b2 =  1.0           / a0;
    bq.a1 = -2.0 * c.cosw  / a0;
    bq.a2 = (1.0 - c.alpha) / a0;
    return IirFilter(bq);
}

// ---------------------------------------------------------------------------
// IirFilter::bandpass  (constant 0 dB peak gain, RBJ)
// ---------------------------------------------------------------------------
IirFilter IirFilter::bandpass(double fs, double loHz, double hiHz)
{
    const double f0 = std::sqrt(loHz * hiHz);
    const double Q  = f0 / (hiHz - loHz);
    auto c = rbj(fs, f0, Q);

    const double a0 = 1.0 + c.alpha;
    Biquad bq;
    bq.b0 =  c.alpha           / a0;
    bq.b1 =  0.0;
    bq.b2 = -c.alpha           / a0;
    bq.a1 = -2.0 * c.cosw      / a0;
    bq.a2 = (1.0 - c.alpha)    / a0;
    return IirFilter(bq);
}

// ---------------------------------------------------------------------------
// IirFilter::highpass
// ---------------------------------------------------------------------------
IirFilter IirFilter::highpass(double fs, double fc, double Q)
{
    auto c = rbj(fs, fc, Q);

    const double a0 = 1.0 + c.alpha;
    Biquad bq;
    bq.b0 =  (1.0 + c.cosw) / 2.0 / a0;
    bq.b1 = -(1.0 + c.cosw)        / a0;
    bq.b2 =  (1.0 + c.cosw) / 2.0 / a0;
    bq.a1 = -2.0 * c.cosw          / a0;
    bq.a2 = (1.0 - c.alpha)        / a0;
    return IirFilter(bq);
}

} // namespace studio
