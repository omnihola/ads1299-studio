#include <QtTest/QtTest>
#include "app/SessionController.h"
#include "core/acquisition/SimulatedSource.h"
#include "core/device/DeviceConfig.h"

using namespace studio;

class TestStartupRate : public QObject
{
    Q_OBJECT

private slots:
    void startStreamingSyncsSourceToConfigRate()
    {
        // Construct a SessionController with SimulatedSource.
        // Do NOT call applyConfig — rely on the default config (500 SPS).
        auto* source = new SimulatedSource(1);
        SessionController ctrl(source);

        // Verify the configured rate is 500 SPS.
        QCOMPARE(ctrl.config().sampleRate(), 500);

        // Start streaming. This should sync the source to 500 SPS.
        ctrl.startStreaming();

        // Wait for the queued invocations to execute on the worker thread.
        // At 500 SPS over ~300ms, we expect ~150 samples.
        QTest::qWait(300);

        // Check the display buffer size.
        // At 500 SPS and ~300ms, we should have at least 130 samples
        // (well above the ~75 that would result from the old 250 default).
        // We use 130 instead of 150 to account for minor timing variations.
        int bufferSize = ctrl.displayBuffer().size();
        QVERIFY2(bufferSize > 130,
                 qPrintable(QString("Expected >130 samples at 500 SPS over 300ms, got %1")
                                .arg(bufferSize)));

        // Clean up.
        ctrl.stopStreaming();
    }
};

QTEST_MAIN(TestStartupRate)
#include "test_startuprate.moc"
