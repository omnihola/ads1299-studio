// tests/test_highrate.cpp
//
// Verifies that applyConfig(cfg.withSampleRate(4000)) actually raises the
// acquisition rate through SimulatedSource so the displayBuffer fills at
// 4000 SPS rather than 250 SPS.
//
// At 4000 SPS over ~300 ms the controller should deliver > 300 samples.
// At 250 SPS the same interval delivers only ~75 samples.

#include <QtTest>
#include "app/SessionController.h"
#include "core/acquisition/SimulatedSource.h"

class TestHighRate : public QObject
{
    Q_OBJECT

private slots:
    void highRateDeliversMoreSamples()
    {
        // Arrange: controller with SimulatedSource; applyConfig before streaming
        // so the source rate is raised before the worker thread ticks.
        studio::SessionController ctrl(new studio::SimulatedSource(1));
        ctrl.applyConfig(studio::DeviceConfig().withSampleRate(4000));

        // Act: start streaming and let it run for ~300 ms
        ctrl.startStreaming();
        QTest::qWait(300);

        // Assert: at 4000 SPS × 0.3 s ≈ 1200 samples; require > 300 as a
        // conservative lower bound that is well above the ~75 from 250 SPS.
        const size_t samples = ctrl.displayBuffer().size();
        QVERIFY2(samples > 300,
                 qPrintable(QString("Expected > 300 samples at 4000 SPS but got %1")
                                .arg(static_cast<int>(samples))));

        ctrl.stopStreaming();
    }
};

QTEST_MAIN(TestHighRate)
#include "test_highrate.moc"
