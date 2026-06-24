// tests/test_configfreeze.cpp
//
// TDD guard: applyConfig must be ignored (config frozen) while a recording
// is in progress, because the Recorder fixes the sample rate and µV scaling
// at open() — a mid-recording change would silently desync the BDF time-base.
//
// Headless — runs under QT_QPA_PLATFORM=offscreen (set via ctest ENVIRONMENT).

#include <QtTest>
#include <QTemporaryDir>

#include "app/SessionController.h"
#include "core/acquisition/SimulatedSource.h"
#include "core/device/DeviceConfig.h"
#include "core/recording/SessionMetadata.h"

using namespace studio;

class TestConfigFreeze : public QObject
{
    Q_OBJECT

private slots:
    void applyConfigIgnoredWhileRecording()
    {
        // Arrange — controller streaming from simulated source at default 500 SPS.
        SessionController ctrl(new SimulatedSource(1));
        QCOMPARE(ctrl.config().sampleRate(), 500);  // default

        ctrl.startStreaming();
        QTest::qWait(80);  // let the worker thread warm up

        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString basePath = dir.filePath("sess");
        const SessionMetadata meta = SessionMetadata()
            .withSubjectId("FreezeTest")
            .withSampleRate(ctrl.config().sampleRate());

        QVERIFY(ctrl.startRecording(basePath, meta));
        QCOMPARE(ctrl.state(), State::Recording);
        QTest::qWait(80);  // let a few frames flow

        // Act — attempt to change sample rate mid-recording
        ctrl.applyConfig(DeviceConfig().withSampleRate(4000));

        // Assert — config must be unchanged (frozen during recording)
        QCOMPARE(ctrl.config().sampleRate(), 500);

        // Cleanup / post-recording — change must now be allowed
        ctrl.stopRecording();
        QCOMPARE(ctrl.state(), State::Streaming);

        ctrl.applyConfig(DeviceConfig().withSampleRate(4000));
        QCOMPARE(ctrl.config().sampleRate(), 4000);  // allowed after recording stops

        ctrl.stopStreaming();
    }
};

QTEST_MAIN(TestConfigFreeze)
#include "test_configfreeze.moc"
