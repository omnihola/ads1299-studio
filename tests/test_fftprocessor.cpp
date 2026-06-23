// tests/test_fftprocessor.cpp  — TDD for FftProcessor (Task 20)
//
// Write FIRST so the test fails (RED), then implement FftProcessor to make it pass (GREEN).

#include <QtTest/QtTest>
#include <cmath>
#include <vector>
#include <algorithm>

#include "core/dsp/FftProcessor.h"

class TestFftProcessor : public QObject
{
    Q_OBJECT

private slots:
    // numBins() must equal nfft/2+1
    void numBinsCorrect();

    // binHz(bin, fs) = bin * fs / nfft
    void binHzFormula();

    // Feed a pure 10 Hz sine at fs=256, nfft=256; peak bin must be ~10 Hz
    void peakBinMatchesTone();
};

void TestFftProcessor::numBinsCorrect()
{
    studio::FftProcessor proc(256);
    QCOMPARE(proc.numBins(), 129);   // 256/2 + 1
}

void TestFftProcessor::binHzFormula()
{
    studio::FftProcessor proc(256);
    // bin 10, fs=256, nfft=256 → 10 * 256 / 256 = 10.0
    QCOMPARE(proc.binHz(10, 256.0), 10.0);
}

void TestFftProcessor::peakBinMatchesTone()
{
    const int nfft = 256;
    const double fs = 256.0;
    const double toneHz = 10.0;

    // Generate 256 samples of a 10 Hz pure sine (amplitude 1.0)
    std::vector<double> samples(nfft);
    for (int n = 0; n < nfft; ++n) {
        samples[n] = std::sin(2.0 * M_PI * toneHz * static_cast<double>(n) / fs);
    }

    studio::FftProcessor proc(nfft);
    auto power = proc.powerSpectrum(samples);

    QCOMPARE(static_cast<int>(power.size()), proc.numBins());

    // Find bin with max power (skip DC bin 0)
    int peakBin = 1;
    for (int k = 2; k < proc.numBins(); ++k) {
        if (power[k] > power[peakBin]) {
            peakBin = k;
        }
    }

    double peakFreq = proc.binHz(peakBin, fs);
    // Bin width is 1 Hz at fs=256, nfft=256; allow ±1.5 Hz tolerance
    QVERIFY2(std::abs(peakFreq - toneHz) <= 1.5,
             qPrintable(QString("Expected ~%1 Hz, got %2 Hz (bin %3)")
                            .arg(toneHz).arg(peakFreq).arg(peakBin)));
}

QTEST_GUILESS_MAIN(TestFftProcessor)
#include "test_fftprocessor.moc"
