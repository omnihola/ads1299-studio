#pragma once
// src/ui/gl/AutoScale.h
//
// Fast-attack / slow-release envelope for the per-channel auto-scale.
// Header-only (no Qt, no GL dependencies) so it is unit-testable standalone.
//
// autoScaleStep computes the next scale value from the current scale and the
// channel's current window peak:
//
//   target = max(signalPeakUv, floorUv) / fillFraction
//   if target > current:  next = current + (target - current) * attack   [fast]
//   if target <= current: next = current + (target - current) * release  [slow]
//   result is never below floorUv
//
// Typical caller defaults:
//   floorUv      = 2.0    µV — prevents amplifying noise to infinity
//   fillFraction = 0.85   — peak fills ~85 % of the half-lane
//   attack       = 0.50   — fast catch-up when signal grows
//   release      = 0.05   — slow decay when signal shrinks

#include <algorithm>
#include <cmath>

namespace studio::gl {

inline double autoScaleStep(double currentScaleUv,
                             double signalPeakUv,
                             double floorUv,
                             double fillFraction,
                             double attack,
                             double release)
{
    // Clamp peak to floor so we always target at least floorUv/fillFraction.
    const double effectivePeak = std::max(signalPeakUv, floorUv);

    // Target lane-edge scale: the µV value that would sit at the lane edge.
    const double target = (fillFraction > 0.0) ? (effectivePeak / fillFraction)
                                                : effectivePeak;

    // Choose coefficient: fast attack when signal grows, slow release when it shrinks.
    const double coeff = (target > currentScaleUv) ? attack : release;

    const double next = currentScaleUv + (target - currentScaleUv) * coeff;

    // Safety floor: never let the scale drop below floorUv.
    return std::max(next, floorUv);
}

} // namespace studio::gl
