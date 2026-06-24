// tests/test_decimate.cpp
//
// Covers studio::decimateMinMaxIndexed / decimateMinMax — the min/max envelope
// decimation that feeds the GL waveform. Previously untested.

#include <QtTest/QtTest>
#include <cmath>

#include "core/dsp/Decimate.h"

using namespace studio;

class TestDecimate : public QObject
{
    Q_OBJECT
private slots:
    void guards_emptyAndNonPositiveMaxPoints()
    {
        QVERIFY(decimateMinMaxIndexed({}, 10).values.isEmpty());
        QVERIFY(decimateMinMaxIndexed(QVector<double>{1.0, 2.0}, 0).values.isEmpty());
        QVERIFY(decimateMinMaxIndexed(QVector<double>{1.0, 2.0}, -5).values.isEmpty());
        QVERIFY(decimateMinMax(QVector<double>{1.0}, 0).isEmpty());
    }

    void copyThroughWhenSmallEnough()
    {
        const QVector<double> in{3.0, 1.0, 4.0, 1.0, 5.0};
        const auto s = decimateMinMaxIndexed(in, 10);
        QCOMPARE(s.values, in);
        QCOMPARE(s.indices.size(), in.size());
        for (int i = 0; i < in.size(); ++i) QCOMPARE(s.indices[i], i);
    }

    void neverExceedsMaxPointsAndIndicesNonDecreasing()
    {
        QVector<double> in;
        for (int i = 0; i < 1000; ++i) in.append(std::sin(i * 0.1));

        const int maxPoints = 100;
        const auto s = decimateMinMaxIndexed(in, maxPoints);

        QVERIFY2(s.values.size() <= maxPoints, "output must not exceed maxPoints");
        QCOMPARE(s.values.size(), s.indices.size());
        for (int k = 1; k < s.indices.size(); ++k) {
            QVERIFY2(s.indices[k] >= s.indices[k - 1], "indices must be non-decreasing");
        }
        for (int idx : s.indices) {
            QVERIFY(idx >= 0 && idx < in.size());
        }
        // Each emitted value must equal the original sample at its recorded index.
        for (int k = 0; k < s.values.size(); ++k) {
            QCOMPARE(s.values[k], in[s.indices[k]]);
        }
    }

    void preservesGlobalExtrema()
    {
        // Flat signal with a single tall positive and negative spike — min/max
        // decimation MUST keep both (this is why the GL scope uses it).
        QVector<double> in(500, 0.0);
        in[250] = 999.0;
        in[100] = -777.0;

        const auto vals = decimateMinMax(in, 40);
        QVERIFY2(vals.contains(999.0),  "positive spike must survive decimation");
        QVERIFY2(vals.contains(-777.0), "negative spike must survive decimation");
    }

    void wrapperEqualsIndexedValues()
    {
        QVector<double> in;
        for (int i = 0; i < 300; ++i) in.append(static_cast<double>(i % 7));
        QCOMPARE(decimateMinMax(in, 50), decimateMinMaxIndexed(in, 50).values);
    }
};

QTEST_MAIN(TestDecimate)
#include "test_decimate.moc"
