#pragma once
// src/core/dsp/Decimate.h
//
// Min/max envelope decimation for display purposes.
// Pure Qt6::Core function — no GUI dependency.

#include <QVector>

namespace studio {

// Min/max envelope decimation.
//
// If samples.size() <= maxPoints, returns a copy unchanged.
// Otherwise, divides samples into (maxPoints/2) buckets and emits
// [min, max] per bucket in index order, so the result is at most
// maxPoints elements and the global extremes are always preserved.
//
// Guards:
//   maxPoints <= 0  → returns {}
//   samples empty   → returns {}
QVector<double> decimateMinMax(const QVector<double>& samples, int maxPoints);

} // namespace studio
