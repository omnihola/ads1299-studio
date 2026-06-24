// tests/test_displayfilter.cpp
// QtTest — core-only, no GUI.

#include <QtTest/QtTest>
#include <cmath>

#include "core/dsp/DisplayFilterChain.h"

class TestDisplayFilter : public QObject {
    Q_OBJECT

private:
    // Feed N samples of a sine at frequency f through channel ch.
    // Returns the peak amplitude over the latter half (steady state).
    double measureAmplitude(studio::dsp::DisplayFilterChain& chain,
                            int ch, double freq, double fs, int N = 2000)
    {
        double peak = 0.0;
        const int halfN = N / 2;
        for (int i = 0; i < N; ++i) {
            const double x = std::sin(2.0 * M_PI * freq * i / fs);
            const double y = chain.process(ch, x);
            if (i >= halfN) {
                peak = std::max(peak, std::abs(y));
            }
        }
        return peak;
    }

private slots:

    // Notch at 50 Hz should suppress 50 Hz sine to < 0.3;
    // after reset a 10 Hz sine should pass with amplitude in [0.8, 1.2].
    void notchRejects50Passes10()
    {
        studio::dsp::DisplayFilterChain chain;
        chain.configure(8, 1000.0, 50.0, 0.0, 0.0);

        const double amp50 = measureAmplitude(chain, 0, 50.0, 1000.0);
        QVERIFY2(amp50 < 0.3,
                 qPrintable(QString("50 Hz amplitude should be < 0.3, got %1").arg(amp50)));

        chain.reset();
        const double amp10 = measureAmplitude(chain, 0, 10.0, 1000.0);
        QVERIFY2(amp10 >= 0.8 && amp10 <= 1.2,
                 qPrintable(QString("10 Hz amplitude should be in [0.8,1.2], got %1").arg(amp10)));
    }

    // Bandpass 8–13 Hz (alpha): 10 Hz passes (> 0.5); 40 Hz rejected (< 0.3).
    void bandpassAlphaPasses10Rejects40()
    {
        studio::dsp::DisplayFilterChain chain;
        chain.configure(8, 1000.0, 0.0, 8.0, 13.0);

        const double amp10 = measureAmplitude(chain, 0, 10.0, 1000.0);
        QVERIFY2(amp10 > 0.5,
                 qPrintable(QString("10 Hz amplitude should be > 0.5, got %1").arg(amp10)));

        chain.reset();
        const double amp40 = measureAmplitude(chain, 0, 40.0, 1000.0);
        QVERIFY2(amp40 < 0.3,
                 qPrintable(QString("40 Hz amplitude should be < 0.3, got %1").arg(amp40)));
    }

    // When all filters are off: enabled() == false and process() is exact passthrough.
    void passthroughWhenOff()
    {
        studio::dsp::DisplayFilterChain chain;
        chain.configure(8, 1000.0, 0.0, 0.0, 0.0);

        QVERIFY2(!chain.enabled(), "enabled() should be false when all filters are off");

        const double result = chain.process(0, 1.234);
        QCOMPARE(result, 1.234);
    }

    // Channel 1's first output must equal a fresh-filter first output —
    // channel 0's accumulated state must not bleed into channel 1.
    void channelsIndependent()
    {
        studio::dsp::DisplayFilterChain chain;
        chain.configure(8, 1000.0, 0.0, 8.0, 13.0);

        // Build up state on channel 0 only
        for (int i = 0; i < 500; ++i) {
            chain.process(0, 100.0 * std::sin(2.0 * M_PI * 10.0 * i / 1000.0));
        }

        // Channel 1 has received zero samples; its state should be pristine.
        // A fresh chain's channel 1 first output must match.
        const double input = 0.5;

        studio::dsp::DisplayFilterChain fresh;
        fresh.configure(8, 1000.0, 0.0, 8.0, 13.0);

        const double ch1Output   = chain.process(1, input);
        const double freshOutput = fresh.process(1, input);

        QCOMPARE(ch1Output, freshOutput);
    }

    // Out-of-range channel index must return the input unchanged.
    void outOfRangePassthrough()
    {
        studio::dsp::DisplayFilterChain chain;
        chain.configure(8, 1000.0, 50.0, 1.0, 40.0);

        const double result = chain.process(99, 5.0);
        QCOMPARE(result, 5.0);
    }
};

QTEST_MAIN(TestDisplayFilter)
#include "test_displayfilter.moc"
