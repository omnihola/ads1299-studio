// src/core/dsp/Decimate.cpp
//
// Min/max envelope decimation — see Decimate.h.

#include "core/dsp/Decimate.h"

#include <algorithm>
#include <limits>

namespace studio {

DecimatedSeries decimateMinMaxIndexed(const QVector<double>& samples, int maxPoints)
{
    // Guards
    if (maxPoints <= 0 || samples.isEmpty()) {
        return {};
    }

    const int n = samples.size();

    // Copy-through when already small enough
    if (n <= maxPoints) {
        DecimatedSeries s;
        s.values  = samples;
        s.indices.resize(n);
        for (int i = 0; i < n; ++i) {
            s.indices[i] = i;
        }
        return s;
    }

    // Number of buckets: each bucket contributes up to 2 output values [min, max]
    const int bucketCount = std::max(1, maxPoints / 2);

    DecimatedSeries s;
    s.values.reserve(bucketCount * 2);
    s.indices.reserve(bucketCount * 2);

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
            s.values.append(minVal);  s.indices.append(minIdx);
            s.values.append(maxVal);  s.indices.append(maxIdx);
        } else {
            s.values.append(maxVal);  s.indices.append(maxIdx);
            s.values.append(minVal);  s.indices.append(minIdx);
        }
    }

    // Fix 2: guarantee output never exceeds maxPoints
    if (s.values.size() > maxPoints) {
        s.values.resize(maxPoints);
        s.indices.resize(maxPoints);
    }

    return s;
}

QVector<double> decimateMinMax(const QVector<double>& samples, int maxPoints)
{
    return decimateMinMaxIndexed(samples, maxPoints).values;
}

} // namespace studio
