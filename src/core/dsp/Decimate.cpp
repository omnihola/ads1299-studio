// src/core/dsp/Decimate.cpp
//
// Min/max envelope decimation — see Decimate.h.

#include "core/dsp/Decimate.h"

#include <algorithm>
#include <limits>

namespace studio {

QVector<double> decimateMinMax(const QVector<double>& samples, int maxPoints)
{
    // Guards
    if (maxPoints <= 0 || samples.isEmpty()) {
        return {};
    }

    // Copy-through when already small enough
    if (samples.size() <= maxPoints) {
        return samples;
    }

    // Number of buckets: each bucket contributes up to 2 output values [min, max]
    const int bucketCount = std::max(1, maxPoints / 2);
    const int n           = samples.size();

    QVector<double> result;
    result.reserve(bucketCount * 2);

    for (int b = 0; b < bucketCount; ++b) {
        // Compute slice [start, end) for this bucket
        const int start = (b * n) / bucketCount;
        const int end   = ((b + 1) * n) / bucketCount;

        if (start >= end) {
            continue; // empty slice (shouldn't happen given guards above)
        }

        // Find min and max values and their indices within the slice
        int    minIdx = start;
        int    maxIdx = start;
        double minVal = samples[start];
        double maxVal = samples[start];

        for (int i = start + 1; i < end; ++i) {
            const double v = samples[i];
            if (v < minVal) { minVal = v; minIdx = i; }
            if (v > maxVal) { maxVal = v; maxIdx = i; }
        }

        // Emit in index order so the waveform envelope follows the original signal
        if (minIdx <= maxIdx) {
            result.append(minVal);
            result.append(maxVal);
        } else {
            result.append(maxVal);
            result.append(minVal);
        }
    }

    return result;
}

} // namespace studio
