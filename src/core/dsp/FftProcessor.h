#pragma once
// src/core/dsp/FftProcessor.h
//
// FftProcessor wraps KISS FFT real-FFT to compute power spectra from EEG
// sample buffers. A Hann window is applied internally before the FFT so
// spectral leakage is suppressed without requiring callers to window data.
//
// Non-copyable (owns a kiss_fftr_cfg allocation).

#include <vector>

struct kiss_fftr_state; // forward-declare; defined in kiss_fftr.h

namespace studio {

class FftProcessor
{
public:
    /// Allocates a KISS FFT real-FFT plan for @p nfft points.
    /// @p nfft must be even and >= 2.
    explicit FftProcessor(int nfft);
    ~FftProcessor();

    // Non-copyable — owns the kiss_fftr_cfg allocation.
    FftProcessor(const FftProcessor&)            = delete;
    FftProcessor& operator=(const FftProcessor&) = delete;

    /// Apply a Hann window to the input, run a real FFT, and return the
    /// power (magnitude^2) for each of the nfft/2+1 frequency bins.
    ///
    /// If @p samples.size() != nfft the data is zero-padded or truncated to
    /// exactly nfft points before processing.
    ///
    /// Returns a vector of length numBins() = nfft/2+1.
    std::vector<double> powerSpectrum(const std::vector<double>& samples) const;

    /// Frequency of bin @p bin in Hz for a signal sampled at @p fs Hz.
    /// Returns bin * fs / nfft.
    double binHz(int bin, double fs) const;

    /// Number of output bins = nfft/2 + 1.
    int numBins() const;

    /// The FFT length this processor was created for.
    int nfft() const;

private:
    int            nfft_;
    kiss_fftr_state* cfg_;          // raw pointer; freed in dtor
    std::vector<float> window_;     // precomputed Hann coefficients, length nfft_
};

} // namespace studio
