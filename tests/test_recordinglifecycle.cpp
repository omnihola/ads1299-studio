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
#include "core/acquisition/EegFrame.h"
#include "core/acquisition/SimulatedSource.h"
#include "core/recording/Recorder.h"
#include "core/recording/SessionMetadata.h"

extern "C" {
#include "edflib.h"
}

using namespace studio;

class TestRecordingLifecycle : public QObject
{
    Q_OBJECT

private slots:
    void test_dropDuringRecordingAnnotated()
    {
        // Arrange — unit test of Recorder::writeGap directly (deterministic,
        // no timing dependency). Opens a recorder, writes a known batch, calls
        // writeGap(K), closes, and asserts paddedSamples == K in meta.json and
        // a "drop:" annotation was stored.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const int sampleRate = 250;
        auto meta = SessionMetadata()
            .withSubjectId("DropTest")
            .withSampleRate(sampleRate);

        const QString basePath = dir.filePath("drop_sess");

        Recorder rec;
        QVERIFY(rec.open(basePath, meta));

        // Write one real batch (2 frames).
        EegFrameBatch batch;
        batch.reserve(2);
        EegFrame f1{};
        f1.seq = 0;
        EegFrame f2{};
        f2.seq = 1;
        batch.push_back(f1);
        batch.push_back(f2);
        rec.writeBatch(batch);

        // Simulate a gap of 10 samples (e.g., seq jumped from 1 to 11).
        const quint32 kGap = 10;
        rec.writeGap(kGap);
        rec.addAnnotation(0.008, QString("drop: %1 samples").arg(kGap));

        rec.close();

        // Assert — meta.json has paddedSamples >= kGap.
        QFile mf(basePath + ".meta.json");
        QVERIFY(mf.open(QIODevice::ReadOnly));
        const auto doc = QJsonDocument::fromJson(mf.readAll());
        QVERIFY2(!doc.isNull(), "meta.json must be valid JSON");
        const QJsonObject obj = doc.object();

        QVERIFY2(obj.contains("paddedSamples"), "meta.json must contain paddedSamples");
        const qint64 padded = obj["paddedSamples"].toInteger();
        QVERIFY2(padded >= static_cast<qint64>(kGap),
                 qPrintable(QString("paddedSamples %1 < expected %2").arg(padded).arg(kGap)));

        // Assert — annotations array contains a "drop:" entry.
        const QJsonArray annotations = obj["annotations"].toArray();
        bool foundDrop = false;
        for (const auto& ann : annotations) {
            const QString label = ann.toObject()["label"].toString();
            if (label.startsWith("drop:")) {
                foundDrop = true;
                break;
            }
        }
        QVERIFY2(foundDrop, "meta.json annotations must contain a 'drop:' entry");

        // Assert — BDF sample count equals real + padded samples.
        const QString bdfPath = basePath + ".bdf";
        edflib_hdr_t hdr;
        const int rh = edfopen_file_readonly(bdfPath.toUtf8().constData(),
                                             &hdr, EDFLIB_READ_ANNOTATIONS);
        QVERIFY2(rh >= 0, "BDF must be readable by EDFlib");
        // total signal duration: (realSamples + paddedSamples) / sampleRate
        const qint64 totalSamples = obj["totalSamplesPerChannel"].toInteger() + padded;
        // BDF stores duration as datarecords * samplefrequency; allow >= total
        const qint64 bdfSamples = static_cast<qint64>(hdr.datarecords_in_file) * sampleRate;
        QVERIFY2(bdfSamples >= totalSamples,
                 qPrintable(QString("BDF samples %1 < total written %2")
                            .arg(bdfSamples).arg(totalSamples)));
        edfclose_file(rh);
    }

    // With writeCsv=false the BDF + meta.json are still written, but the large
    // per-sample CSV is skipped (for long/high-rate sessions). meta.json's
    // csvFile must be empty so the metadata stays honest.
    void recordingWithoutCsvSkipsCsvFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString basePath = dir.filePath("nocsv_sess");

        auto meta = SessionMetadata().withSubjectId("NoCsv").withSampleRate(250);

        Recorder rec;
        QVERIFY(rec.open(basePath, meta, /*writeCsv=*/false));
        EegFrameBatch batch;
        EegFrame f{};
        f.seq = 0;
        batch.push_back(f);
        rec.writeBatch(batch);
        rec.close();

        QVERIFY2(QFile::exists(basePath + ".bdf"),  "BDF must still be written");
        QVERIFY2(QFile::exists(basePath + ".meta.json"), "meta.json must still be written");
        QVERIFY2(!QFile::exists(basePath + ".csv"), "CSV must NOT be written when disabled");

        QFile mf(basePath + ".meta.json");
        QVERIFY(mf.open(QIODevice::ReadOnly));
        const QJsonObject obj = QJsonDocument::fromJson(mf.readAll()).object();
        QVERIFY2(obj["csvFile"].toString().isEmpty(),
                 "meta.json csvFile must be empty when CSV is disabled");
    }

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
