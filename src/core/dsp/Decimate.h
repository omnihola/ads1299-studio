#pragma once
// src/core/dsp/Decimate.h
//
// Min/max envelope decimation for display purposes.
// Pure Qt6::Core function — no GUI dependency.

#include <QVector>

namespace studio {

// Result type returned by decimateMinMaxIndexed.
// values[k] is the decimated sample value; indices[k] is its index in
// the original samples array. Both vectors are always the same size and
// index order is strictly non-decreasing.
struct DecimatedSeries {
    QVector<double> values;   // decimated sample values (envelope)
    QVector<int>    indices;  // index into the ORIGINAL samples for each emitted value
};

// Index-aware min/max envelope decimation (primary algorithm).
//
// Identical bucketing logic to decimateMinMax but also records, for
// every emitted value, the original sample index it came from.  Indices
// are emitted in strictly non-decreasing order (index order within each
// bucket).
//
// Guarantee: result.values.size() == result.indices.size() <= maxPoints
//
// Guards:
//   maxPoints <= 0  → returns {{}, {}}
//   samples empty   → returns {{}, {}}
//   samples.size() <= maxPoints → copy-through (values==samples, indices=0..n-1)
DecimatedSeries decimateMinMaxIndexed(const QVector<double>& samples, int maxPoints);

// Min/max envelope decimation (convenience wrapper).
//
// Equivalent to decimateMinMaxIndexed(samples, maxPoints).values.
// All existing behaviour and tests are unchanged.
//
// Guards:
//   maxPoints <= 0  → returns {}
//   samples empty   → returns {}
QVector<double> decimateMinMax(const QVector<double>& samples, int maxPoints);

} // namespace studio
