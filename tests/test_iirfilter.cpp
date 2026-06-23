#include <QtTest/QtTest>
#include <cmath>
#include "core/dsp/IirFilter.h"

namespace {

// Feed N samples of a unit-amplitude sine at frequency f Hz into filter.
// Returns the peak amplitude over the latter half (steady state).
double measureAmplitude(studio::IirFilter& filt, double fs, double freqHz, int N = 2000)
{
    double peak = 0.0;
    int half = N / 2;
    for (int i = 0; i < N; ++i) {
        double x = std::sin(2.0 * M_PI * freqHz * i / fs);
        double y = filt.process(x);
        if (i >= half) {
            double ay = std::abs(y);
            if (ay > peak) peak = ay;
        }
    }
    return peak;
}

} // anonymous namespace

class TestIirFilter : public QObject
{
    Q_OBJECT

private slots:

    // notch at 50 Hz should strongly attenuate a 50 Hz input
    void notchRejectsTargetFrequency()
    {
        auto filt = studio::IirFilter::notch(1000.0, 50.0, 5.0);
        double amp = measureAmplitude(filt, 1000.0, 50.0, 2000);
        QVERIFY2(amp < 0.2, qPrintable(QString("Expected < 0.2, got %1").arg(amp)));
    }

    // same notch filter should pass a 10 Hz signal near unity
    void notchPassesOtherFrequency()
    {
        auto filt = studio::IirFilter::notch(1000.0, 50.0, 5.0);
        double amp = measureAmplitude(filt, 1000.0, 10.0, 2000);
        QVERIFY2(amp >= 0.8 && amp <= 1.2,
                 qPrintable(QString("Expected [0.8, 1.2], got %1").arg(amp)));
    }

    // bandpass 8-12 Hz: 10 Hz passes, 40 Hz rejected
    void bandpassPassesCenterRejectsFar()
    {
        auto filt = studio::IirFilter::bandpass(1000.0, 8.0, 12.0);
        double amp10 = measureAmplitude(filt, 1000.0, 10.0, 2000);
        QVERIFY2(amp10 > 0.7,
                 qPrintable(QString("10 Hz: expected > 0.7, got %1").arg(amp10)));

        filt.reset();

        double amp40 = measureAmplitude(filt, 1000.0, 40.0, 2000);
        QVERIFY2(amp40 < 0.3,
                 qPrintable(QString("40 Hz: expected < 0.3, got %1").arg(amp40)));
    }

    // after reset, the first output for a unit impulse equals b0
    void resetClearsState()
    {
        auto filt = studio::IirFilter::notch(1000.0, 50.0, 5.0);
        // run a few samples to dirty state
        for (int i = 0; i < 50; ++i) filt.process(1.0);

        filt.reset();

        // fresh filter: impulse x=1 → y = b0*1 + z1 = b0*1 + 0
        double y0 = filt.process(1.0);
        double b0 = filt.biquad().b0;
        QVERIFY2(std::abs(y0 - b0) < 1e-12,
                 qPrintable(QString("After reset: expected %1, got %2").arg(b0).arg(y0)));
    }
};

QTEST_MAIN(TestIirFilter)
#include "test_iirfilter.moc"
