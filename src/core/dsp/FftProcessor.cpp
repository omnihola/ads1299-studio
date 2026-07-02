// src/core/dsp/FftProcessor.cpp
#include "core/dsp/FftProcessor.h"

#include <cmath>
#include <stdexcept>

#include "kiss_fftr.h"

namespace studio {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

FftProcessor::FftProcessor(int nfft)
    : nfft_(nfft)
    , cfg_(nullptr)
{
    if (nfft_ < 2 || (nfft_ % 2) != 0) {
        throw std::invalid_argument("FftProcessor: nfft must be even and >= 2");
    }

    cfg_ = kiss_fftr_alloc(nfft_, /*inverse=*/0, /*mem=*/nullptr, /*lenmem=*/nullptr);
    if (!cfg_) {
        throw std::runtime_error("FftProcessor: kiss_fftr_alloc failed");
    }

    // Precompute Hann window coefficients.
    // w[n] = 0.5 * (1 - cos(2*pi*n / (N-1)))  for n = 0 .. N-1.
    window_.resize(static_cast<size_t>(nfft_));
    const double twoPiOverNm1 = 2.0 * M_PI / static_cast<double>(nfft_ - 1);
    for (int n = 0; n < nfft_; ++n) {
        window_[static_cast<size_t>(n)] =
            static_cast<float>(0.5 * (1.0 - std::cos(twoPiOverNm1 * static_cast<double>(n))));
    }
}

FftProcessor::~FftProcessor()
{
    if (cfg_) {
        kiss_fftr_free(cfg_);
        cfg_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Core computation
// ---------------------------------------------------------------------------

std::vector<double> FftProcessor::powerSpectrum(const std::vector<double>& samples) const
{
    // Remove the per-window DC offset before applying the Hann window. EEG front
    // ends can carry large electrode offsets; windowing the raw offset leaks a
    // huge low-frequency skirt into the live spectrum.
    std::vector<float> windowed(static_cast<size_t>(nfft_), 0.0f);
    const int copyLen = static_cast<int>(
        std::min(samples.size(), static_cast<size_t>(nfft_)));

    double mean = 0.0;
    for (int n = 0; n < copyLen; ++n) {
        mean += samples[static_cast<size_t>(n)];
    }
    if (copyLen > 0) {
        mean /= static_cast<double>(copyLen);
    }

    // Copy/pad/truncate input to exactly nfft_ floats, applying the Hann window.
    for (int n = 0; n < copyLen; ++n) {
        windowed[static_cast<size_t>(n)] =
            static_cast<float>(samples[static_cast<size_t>(n)] - mean) *
            window_[static_cast<size_t>(n)];
    }

    // Run the real FFT; output has nfft/2+1 complex bins.
    const int numBinsOut = nfft_ / 2 + 1;
    std::vector<kiss_fft_cpx> freqdata(static_cast<size_t>(numBinsOut));
    kiss_fftr(cfg_, windowed.data(), freqdata.data());

    // Compute power (magnitude^2) for each bin.
    std::vector<double> power(static_cast<size_t>(numBinsOut));
    for (int k = 0; k < numBinsOut; ++k) {
        const double r = static_cast<double>(freqdata[static_cast<size_t>(k)].r);
        const double i = static_cast<double>(freqdata[static_cast<size_t>(k)].i);
        power[static_cast<size_t>(k)] = r * r + i * i;
    }
    return power;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

double FftProcessor::binHz(int bin, double fs) const
{
    return static_cast<double>(bin) * fs / static_cast<double>(nfft_);
}

int FftProcessor::numBins() const
{
    return nfft_ / 2 + 1;
}

int FftProcessor::nfft() const
{
    return nfft_;
}

} // namespace studio
