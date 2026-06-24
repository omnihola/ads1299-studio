// tests/test_acquisitionpanel.cpp
//
// QtTest — TDD for AcquisitionPanel pure helpers + widget-level live-config test.
// Build: compiled with AcquisitionPanel.cpp directly into this test target.
// Run with: QT_QPA_PLATFORM=offscreen ctest -R acquisitionpanel

#include <QtTest/QtTest>
#include <QComboBox>
#include <cmath>

#include "ui/AcquisitionPanel.h"
#include "app/SessionController.h"
#include "core/acquisition/SimulatedSource.h"

class TestAcquisitionPanel : public QObject
{
    Q_OBJECT

private slots:

    // ------------------------------------------------------------------
    // Pure helpers — no widget needed
    // ------------------------------------------------------------------

    void resolutionForGain24()
    {
        // (4.5 / (24 * 8388608)) * 1e6 ≈ 0.022352 µV
        const double result = studio::AcquisitionPanel::resolutionMicrovolts(24);
        QVERIFY(qAbs(result - 0.0223517) < 1e-5);
    }

    void resolutionForGain1()
    {
        // (4.5 / (1 * 8388608)) * 1e6 ≈ 0.5364 µV
        const double result = studio::AcquisitionPanel::resolutionMicrovolts(1);
        QVERIFY(qAbs(result - 0.5364) < 1e-3);
    }

    void inputRange()
    {
        // inputRangeMillivolts(gain) = (4.5 / gain) * 1e3
        const double r24 = studio::AcquisitionPanel::inputRangeMillivolts(24);
        QVERIFY(qAbs(r24 - 187.5) < 1e-6);

        const double r1 = studio::AcquisitionPanel::inputRangeMillivolts(1);
        QVERIFY(qAbs(r1 - 4500.0) < 1e-6);
    }

    void nyquist()
    {
        QCOMPARE(studio::AcquisitionPanel::nyquistHz(500),  250.0);
        QCOMPARE(studio::AcquisitionPanel::nyquistHz(16000), 8000.0);
    }

    // ------------------------------------------------------------------
    // Widget-level: changing the combo applies live config
    // ------------------------------------------------------------------

    void appliesLiveConfig()
    {
        studio::SessionController ctrl(new studio::SimulatedSource(1));

        // Default SPS should be 500 per DeviceConfig defaults.
        QCOMPARE(ctrl.config().sampleRate(), 500);

        studio::AcquisitionPanel panel(&ctrl);
        panel.show();

        // Use the public test hook to select 1000 Hz.
        panel.setSampleRateSelection(1000);

        // Give the event loop a moment to propagate the signal.
        QTest::qWait(20);

        QCOMPARE(ctrl.config().sampleRate(), 1000);
    }
};

QTEST_MAIN(TestAcquisitionPanel)
#include "test_acquisitionpanel.moc"
