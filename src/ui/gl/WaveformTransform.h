#pragma once
// src/ui/gl/WaveformTransform.h
//
// Pure, stateless helpers that map (sampleIndex, µV, channel) → NDC coordinates.
// Header-only so they are usable in unit tests without linking Qt or OpenGL.
//
// Coordinate conventions:
//   NDC x : -1 (oldest sample in window) → +1 (newest)
//   NDC y : +1 (top) → -1 (bottom)
//   Channel 0 is placed at the TOP lane; channel N-1 at the BOTTOM lane.
//   Each channel occupies a lane of height 2.0/channelCount in NDC units.

namespace studio::gl {

// ---------------------------------------------------------------------------
// X mapping — stretch mode
// ---------------------------------------------------------------------------
// Maps sample position p (0=oldest, W-1=newest) in a window of W samples
// to NDC x in [-1, +1].
// Edge case: W <= 1 always returns -1 (avoids divide-by-zero).
inline float sampleIndexToNdcX(int p, int windowSamples)
{
    if (windowSamples <= 1) {
        return -1.0f;
    }
    return -1.0f + 2.0f * static_cast<float>(p) / static_cast<float>(windowSamples - 1);
}

// ---------------------------------------------------------------------------
// X mapping — scroll / right-aligned mode
// ---------------------------------------------------------------------------
// Maps buffered sample j (0=oldest, n-1=newest) to NDC x given a FIXED
// window capacity W.  The newest sample (j == n-1) is always pinned at +1;
// a sample W-1 positions back from the newest maps to exactly -1.  Samples
// even older land at x < -1 and should be dropped by the caller.
//
// Formula:  x = +1 - 2*(n-1-j)/(W-1)
//
// Edge cases:
//   W <= 1  → returns +1.0f (single-sample degenerate window)
//   n < 1   → undefined; returns +1.0f (caller should guard n>=1)
inline float sampleWindowToNdcX(int j, int n, int windowCapacity)
{
    if (windowCapacity <= 1) {
        return 1.0f;
    }
    return 1.0f - 2.0f * static_cast<float>(n - 1 - j)
                        / static_cast<float>(windowCapacity - 1);
}

// ---------------------------------------------------------------------------
// Y mapping — channel centre
// ---------------------------------------------------------------------------
// Returns the NDC y coordinate of the centre of channel c's lane.
// Channel 0 is at the top (positive y), channel channelCount-1 at the bottom.
// Formula: 1 - (2*c + 1) / channelCount
inline float channelCenterNdcY(int channel, int channelCount)
{
    if (channelCount <= 0) {
        return 0.0f;
    }
    return 1.0f - static_cast<float>(2 * channel + 1) / static_cast<float>(channelCount);
}

// ---------------------------------------------------------------------------
// Y mapping — µV within a lane
// ---------------------------------------------------------------------------
// Maps a microvolt value to NDC y, centred on the channel's lane.
//
//   laneHeightNdc  = 2.0 / channelCount
//   fullScaleUv    = uvPerDiv * divsPerHalfLane
//   y = channelCenter + (uv / fullScaleUv) * (laneHeightNdc / 2)
//
// When uv == +fullScaleUv the point lands exactly at the top lane edge.
// When uv == -fullScaleUv the point lands exactly at the bottom lane edge.
// When uv == 0 the point lands at channelCenterNdcY.
//
// Typical usage: divsPerHalfLane = 1.5  (3 div total visible per channel).
inline float microvoltsToNdcY(double uv,
                               int    channel,
                               int    channelCount,
                               double uvPerDiv,
                               double divsPerHalfLane)
{
    const float  centre         = channelCenterNdcY(channel, channelCount);
    const float  laneHeightNdc  = (channelCount > 0)
                                      ? 2.0f / static_cast<float>(channelCount)
                                      : 2.0f;
    const double fullScaleUv    = uvPerDiv * divsPerHalfLane;
    if (fullScaleUv == 0.0) {
        return centre;
    }
    const float  halfLaneNdc    = laneHeightNdc / 2.0f;
    return centre + static_cast<float>(uv / fullScaleUv) * halfLaneNdc;
}

} // namespace studio::gl
