// tests/test_recordinglifecycle.cpp
// Integration test for the recording lifecycle:
//   SessionController + Recorder + AnnotationStore, end-to-end.
// Headless — runs under QT_QPA_PLATFORM=offscreen (set via ctest ENVIRONMENT).
//
// Proves Parts A + B: streaming → startRecording → addMarker → stopRecording
// produces a .bdf and a .meta.json, and the meta.json "annotations" array has
// at least one entry. Also reads the BDF back via EDFlib to confirm the marker
// landed in the file.

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "app/SessionController.h"
#include "core/acquisition/SimulatedSource.h"
#include "core/recording/SessionMetadata.h"

extern "C" {
#include "edflib.h"
}

using namespace studio;

class TestRecordingLifecycle : public QObject
{
    Q_OBJECT

private slots:
    void recordingLifecycleEndToEnd()
    {
        // Arrange — controller streaming from a deterministic simulated source.
        SessionController ctrl(new SimulatedSource(1));
        ctrl.startStreaming();
        QTest::qWait(150);  // let the worker thread warm up and emit frames

        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        auto meta = SessionMetadata()
            .withSubjectId("LifecycleTest")
            .withSampleRate(250);

        const QString basePath = dir.filePath("sess");

        // Act
        QVERIFY(ctrl.startRecording(basePath, meta));
        QCOMPARE(ctrl.state(), State::Recording);

        QTest::qWait(300);          // collect samples
        QVERIFY2(ctrl.recordedSamples() > 0,
                 "recorder must have received EEG samples while recording");
        ctrl.addMarker("evtA");
        QTest::qWait(200);          // let the marker flush to the writer thread

        ctrl.stopRecording();
        QCOMPARE(ctrl.state(), State::Streaming);
        ctrl.stopStreaming();

        // Assert — files exist
        QVERIFY2(QFile::exists(basePath + ".bdf"), "BDF file must be created");
        QVERIFY2(QFile::exists(basePath + ".meta.json"), "meta.json must be created");

        // Assert — meta.json parses and has an annotations array of length >= 1
        QFile mf(basePath + ".meta.json");
        QVERIFY(mf.open(QIODevice::ReadOnly));
        const auto doc = QJsonDocument::fromJson(mf.readAll());
        QVERIFY2(!doc.isNull(), "meta.json must be valid JSON");
        const QJsonArray annotations = doc.object()["annotations"].toArray();
        QVERIFY2(annotations.size() >= 1,
                 "meta.json annotations array must have at least 1 entry (evtA)");

        // Assert — the BDF file itself contains the annotation.
        const QString bdfPath = basePath + ".bdf";
        edflib_hdr_t hdr;
        const int rh = edfopen_file_readonly(bdfPath.toUtf8().constData(),
                                             &hdr, EDFLIB_READ_ANNOTATIONS);
        QVERIFY2(rh >= 0, "BDF must be readable by EDFlib");
        QVERIFY2(hdr.annotations_in_file >= 1,
                 "BDF must contain at least 1 annotation");
        edfclose_file(rh);
    }
};

QTEST_MAIN(TestRecordingLifecycle)
#include "test_recordinglifecycle.moc"
