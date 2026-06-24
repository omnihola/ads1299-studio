#pragma once

#include "IirFilter.h"

#include <optional>
#include <vector>

namespace studio::dsp {

/**
 * @brief Per-channel display filter chain: optional notch + optional bandpass.
 *
 * Applied to the Monitor view ONLY. Recording path is unaffected.
 * Each channel has its own independent filter instances and state.
 */
class DisplayFilterChain {
public:
    DisplayFilterChain() = default;

    /**
     * @brief Build per-channel chains.
     * @param channels  Number of channels (>= 1).
     * @param fs        Sample rate in Hz.
     * @param notchHz   0 = off, else 50 or 60 (narrow notch, Q=30).
     * @param bpLo      Lower bandpass cutoff in Hz (0 = off).
     * @param bpHi      Upper bandpass cutoff in Hz; bandpass applied only when bpHi > bpLo > 0.
     *
     * Order per channel: notch (if any) -> bandpass (if any).
     */
    void configure(int channels, double fs, double notchHz, double bpLo, double bpHi);

    /**
     * @brief Process one sample through the specified channel's filter chain.
     * @return Filtered value, or x unchanged if channel is out of range or no filters active.
     */
    double process(int channel, double x);

    /**
     * @brief Reset all filter state (call on stream clear / rate change).
     */
    void reset();

    /**
     * @brief Returns true if any filter is active.
     */
    bool enabled() const;

private:
    struct Chain {
        std::optional<studio::IirFilter> notch;
        std::optional<studio::IirFilter> bp;
    };

    std::vector<Chain> chains_;
    bool enabled_ = false;
};

} // namespace studio::dsp
