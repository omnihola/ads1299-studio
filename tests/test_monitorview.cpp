// tests/test_monitorview.cpp
//
// TDD test for the decimateMinMax() utility.
// No GUI widgets — pure core/dsp logic.

#include <QtTest/QtTest>
#include <cmath>

#include "core/dsp/Decimate.h"

class TestDecimate : public QObject
{
    Q_OBJECT

private slots:
    // 1. 10 000-sample signal with embedded spike values → result.size() <= 1000
    void decimateLargeSignal_sizeConstraint()
    {
        QVector<double> samples(10000);
        for (int i = 0; i < samples.size(); ++i) {
            samples[i] = std::sin(2.0 * M_PI * i / 250.0) * 100.0;
        }
        // Embed known extreme values
        samples[5000] = +1e6;
        samples[7000] = -1e6;

        const auto result = studio::decimateMinMax(samples, 1000);
        QVERIFY2(result.size() <= 1000,
                 qPrintable(QString("Expected size <= 1000, got %1").arg(result.size())));
    }

    // 2. Global max (+1e6 at sample[5000]) must be in the output (envelope preserved)
    void decimateLargeSignal_preservesMax()
    {
        QVector<double> samples(10000);
        for (int i = 0; i < samples.size(); ++i) {
            samples[i] = std::sin(2.0 * M_PI * i / 250.0) * 100.0;
        }
        samples[5000] = +1e6;
        samples[7000] = -1e6;

        const auto result = studio::decimateMinMax(samples, 1000);
        const bool hasMax = result.contains(+1e6);
        QVERIFY2(hasMax, "Global max (+1e6) must appear in decimated output");
    }

    // 3. Global min (-1e6 at sample[7000]) must be in the output (envelope preserved)
    void decimateLargeSignal_preservesMin()
    {
        QVector<double> samples(10000);
        for (int i = 0; i < samples.size(); ++i) {
            samples[i] = std::sin(2.0 * M_PI * i / 250.0) * 100.0;
        }
        samples[5000] = +1e6;
        samples[7000] = -1e6;

        const auto result = studio::decimateMinMax(samples, 1000);
        const bool hasMin = result.contains(-1e6);
        QVERIFY2(hasMin, "Global min (-1e6) must appear in decimated output");
    }

    // 4. Small input (size < maxPoints) → copy-through, no decimation
    void decimateSmallSignal_copyThrough()
    {
        QVector<double> small(50);
        for (int i = 0; i < small.size(); ++i) {
            small[i] = static_cast<double>(i);
        }

        const auto result = studio::decimateMinMax(small, 1000);
        QCOMPARE(result.size(), 50);
        // Values must be identical
        for (int i = 0; i < 50; ++i) {
            QCOMPARE(result[i], small[i]);
        }
    }

    // 5. maxPoints == 0 → returns empty
    void decimateZeroMaxPoints_returnsEmpty()
    {
        QVector<double> samples(10000);
        for (int i = 0; i < samples.size(); ++i) {
            samples[i] = static_cast<double>(i);
        }

        const auto result = studio::decimateMinMax(samples, 0);
        QVERIFY2(result.isEmpty(), "decimateMinMax(s, 0) must return an empty vector");
    }

    // 6. Empty input → returns empty regardless of maxPoints
    void decimateEmptyInput_returnsEmpty()
    {
        QVector<double> empty;
        const auto result = studio::decimateMinMax(empty, 1000);
        QVERIFY(result.isEmpty());
    }

    // 7. decimateMinMaxIndexed: sizes match, indices in-range and non-decreasing,
    //    and values[k] == samples[indices[k]] for every k.
    void decimateMinMaxIndexed_indicesConsistent()
    {
        const int N = 10000;
        QVector<double> samples(N);
        for (int i = 0; i < N; ++i) {
            samples[i] = std::sin(2.0 * M_PI * i / 250.0) * 100.0;
        }

        const auto d = studio::decimateMinMaxIndexed(samples, 1000);

        QVERIFY2(d.values.size() == d.indices.size(),
                 qPrintable(QString("values.size()=%1 != indices.size()=%2")
                            .arg(d.values.size()).arg(d.indices.size())));
        QVERIFY2(d.values.size() <= 1000,
                 qPrintable(QString("output size %1 exceeds maxPoints 1000").arg(d.values.size())));

        for (int k = 0; k < d.indices.size(); ++k) {
            QVERIFY2(d.indices[k] >= 0 && d.indices[k] < N,
                     qPrintable(QString("index[%1]=%2 out of range [0,%3)")
                                .arg(k).arg(d.indices[k]).arg(N)));
            if (k > 0) {
                QVERIFY2(d.indices[k] >= d.indices[k - 1],
                         qPrintable(QString("indices not non-decreasing at k=%1: %2 < %3")
                                    .arg(k).arg(d.indices[k]).arg(d.indices[k - 1])));
            }
            QVERIFY2(d.values[k] == samples[d.indices[k]],
                     qPrintable(QString("values[%1]=%2 != samples[indices[%1]]=%3")
                                .arg(k).arg(d.values[k]).arg(samples[d.indices[k]])));
        }
    }

    // 8. maxPoints==1: both decimateMinMax and decimateMinMaxIndexed.values
    //    must return at most 1 element.
    void decimateMaxPointsOne_clampsBothFunctions()
    {
        QVector<double> samples = {1.0, 2.0, 3.0, 4.0, 5.0};

        const auto plain   = studio::decimateMinMax(samples, 1);
        QVERIFY2(plain.size() <= 1,
                 qPrintable(QString("decimateMinMax result size %1 > 1").arg(plain.size())));

        const auto indexed = studio::decimateMinMaxIndexed(samples, 1);
        QVERIFY2(indexed.values.size() <= 1,
                 qPrintable(QString("decimateMinMaxIndexed values size %1 > 1")
                            .arg(indexed.values.size())));
    }
};

QTEST_MAIN(TestDecimate)
#include "test_monitorview.moc"
