#pragma once

#include "Biquad.h"

namespace studio {

/// Single biquad section wrapper with RBJ-cookbook factory designers.
class IirFilter {
public:
    /// Notch (band-reject) filter.
    /// @param fs   sample rate (Hz)
    /// @param f0   notch centre frequency (Hz)
    /// @param Q    quality factor (bandwidth = f0/Q)
    static IirFilter notch(double fs, double f0, double Q);

    /// Bandpass filter (constant 0 dB peak gain, RBJ form).
    /// Centre = sqrt(loHz * hiHz), Q = centre / (hiHz - loHz).
    static IirFilter bandpass(double fs, double loHz, double hiHz);

    /// High-pass filter.
    /// @param fs   sample rate (Hz)
    /// @param fc   corner frequency (Hz)
    /// @param Q    quality factor (default Butterworth ≈ 0.707)
    static IirFilter highpass(double fs, double fc, double Q = 0.707);

    double process(double x) noexcept { return bq_.process(x); }
    void   reset()  noexcept          { bq_.reset(); }

    /// Expose the underlying biquad for coefficient inspection / testing.
    Biquad biquad() const noexcept { return bq_; }

private:
    explicit IirFilter(const Biquad& bq) : bq_(bq) {}

    Biquad bq_;
};

} // namespace studio
