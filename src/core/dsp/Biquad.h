#pragma once

namespace studio {

/// Single second-order IIR section (Direct Form II Transposed).
/// Coefficients are normalized so that a0 = 1.
struct Biquad {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;

    double z1 = 0.0;   // delay line state
    double z2 = 0.0;

    double process(double x) noexcept
    {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0.0; }
};

} // namespace studio
