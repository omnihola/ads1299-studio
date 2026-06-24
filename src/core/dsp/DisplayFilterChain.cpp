// src/core/dsp/DisplayFilterChain.cpp

#include "DisplayFilterChain.h"

namespace studio::dsp {

void DisplayFilterChain::configure(int channels, double fs, double notchHz, double bpLo, double bpHi)
{
    chains_.clear();
    enabled_ = false;

    if (channels <= 0 || fs <= 0.0) {
        return;
    }

    const bool applyNotch = (notchHz > 0.0);
    const bool applyBp    = (bpLo > 0.0 && bpHi > 0.0 && bpHi > bpLo);

    chains_.resize(static_cast<size_t>(channels));

    for (auto& chain : chains_) {
        if (applyNotch) {
            chain.notch = studio::IirFilter::notch(fs, notchHz, 30.0);
        }
        if (applyBp) {
            chain.bp = studio::IirFilter::bandpass(fs, bpLo, bpHi);
        }
    }

    enabled_ = applyNotch || applyBp;
}

double DisplayFilterChain::process(int channel, double x)
{
    if (channel < 0 || static_cast<size_t>(channel) >= chains_.size()) {
        return x;
    }

    Chain& chain = chains_[static_cast<size_t>(channel)];
    double y = x;

    if (chain.notch.has_value()) {
        y = chain.notch->process(y);
    }
    if (chain.bp.has_value()) {
        y = chain.bp->process(y);
    }

    return y;
}

void DisplayFilterChain::reset()
{
    for (auto& chain : chains_) {
        if (chain.notch.has_value()) {
            chain.notch->reset();
        }
        if (chain.bp.has_value()) {
            chain.bp->reset();
        }
    }
}

bool DisplayFilterChain::enabled() const
{
    return enabled_;
}

} // namespace studio::dsp
