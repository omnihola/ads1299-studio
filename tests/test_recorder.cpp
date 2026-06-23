#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/recording/Recorder.h"
#include "core/recording/SessionMetadata.h"
#include "core/acquisition/EegFrame.h"

extern "C" {
#include "edflib.h"
}

using namespace studio;

class TestRecorder : public QObject {
    Q_OBJECT

private slots:

    void recordsBdfReadbackEqual() {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());

        const int kSampleRate = 256;
        const int kChannels   = 8;

        // Build metadata
        auto meta = SessionMetadata()
            .withSubjectId("S_RB")
            .withSampleRate(kSampleRate);

        // Build one batch of 256 frames: ch[c] = c * 1000 + i
        EegFrameBatch batch;
        batch.reserve(kSampleRate);
        for (int i = 0; i < kSampleRate; ++i) {
            EegFrame f;
            f.seq = static_cast<uint32_t>(i);
            for (int c = 0; c < kChannels; ++c) {
                f.ch[c] = c * 1000 + i;
            }
            batch.push_back(f);
        }

        QString basePath = tmpDir.filePath("rec_rb");

        Recorder recorder;
        QVERIFY(recorder.open(basePath, meta));
        recorder.writeBatch(batch);
        recorder.close();

        QCOMPARE(recorder.samplesWritten(), static_cast<quint64>(kSampleRate));

        // --- Read back via EDFlib ---
        QString bdfPath = basePath + ".bdf";
        edflib_hdr_t hdr;
        int readHandle = edfopen_file_readonly(
            bdfPath.toUtf8().constData(),
            &hdr,
            EDFLIB_DO_NOT_READ_ANNOTATIONS);
        QVERIFY2(readHandle >= 0, "edfopen_file_readonly failed");

        // Should report 8 data signals (annotation channel is hidden by EDFlib)
        QCOMPARE(hdr.edfsignals, kChannels);

        // Read back 256 digital samples per channel
        for (int c = 0; c < kChannels; ++c) {
            QVector<int> samples(kSampleRate);
            int got = edfread_digital_samples(readHandle, c, kSampleRate, samples.data());
            QCOMPARE(got, kSampleRate);
            for (int i = 0; i < kSampleRate; ++i) {
                int expected = c * 1000 + i;
                QCOMPARE(samples[i], expected);
            }
        }

        edfclose_file(readHandle);
    }

    void csvAndMetaWritten() {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());

        const int kSampleRate = 256;

        auto meta = SessionMetadata()
            .withSubjectId("S_CSV")
            .withSampleRate(kSampleRate);

        // Build batch
        EegFrameBatch batch;
        batch.reserve(kSampleRate);
        for (int i = 0; i < kSampleRate; ++i) {
            EegFrame f;
            f.seq = static_cast<uint32_t>(i);
            for (int c = 0; c < 8; ++c) f.ch[c] = i;
            batch.push_back(f);
        }

        QString basePath = tmpDir.filePath("rec_csv");

        Recorder recorder;
        QVERIFY(recorder.open(basePath, meta));
        recorder.writeBatch(batch);
        recorder.close();

        // Check CSV: 1 header + 256 data rows = 257 lines
        QString csvPath = basePath + ".csv";
        QVERIFY2(QFile::exists(csvPath), "CSV file missing");

        QFile csvFile(csvPath);
        QVERIFY(csvFile.open(QIODevice::ReadOnly | QIODevice::Text));
        QTextStream ts(&csvFile);
        int lineCount = 0;
        while (!ts.atEnd()) {
            ts.readLine();
            ++lineCount;
        }
        QCOMPARE(lineCount, 257); // header + 256 rows

        // Check meta.json
        QString metaPath = basePath + ".meta.json";
        QVERIFY2(QFile::exists(metaPath), "meta.json missing");

        QFile metaFile(metaPath);
        QVERIFY(metaFile.open(QIODevice::ReadOnly));
        auto doc = QJsonDocument::fromJson(metaFile.readAll());
        QVERIFY(!doc.isNull());

        QJsonObject obj = doc.object();
        QCOMPARE(obj["totalSamplesPerChannel"].toInt(), kSampleRate);
        QCOMPARE(obj["subjectId"].toString(), QString("S_CSV"));
    }

    void openInvalidPathReturnsFalse() {
        Recorder recorder;
        SessionMetadata meta;
        bool result = recorder.open("", meta);
        QVERIFY(!result);
    }

    void metaJsonWrittenOnOpen() {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());

        const int kSampleRate = 256;

        auto meta = SessionMetadata()
            .withSubjectId("S_META")
            .withSampleRate(kSampleRate);

        QString basePath = tmpDir.filePath("rec_meta");

        Recorder recorder;
        QVERIFY(recorder.open(basePath, meta));

        // Before writing any data, read the .meta.json and verify totalSamplesPerChannel exists
        QString metaPath = basePath + ".meta.json";
        QVERIFY2(QFile::exists(metaPath), "meta.json missing immediately after open");

        QFile metaFile(metaPath);
        QVERIFY(metaFile.open(QIODevice::ReadOnly));
        auto doc = QJsonDocument::fromJson(metaFile.readAll());
        metaFile.close();
        QVERIFY(!doc.isNull());

        QJsonObject obj = doc.object();
        QVERIFY2(obj.contains("totalSamplesPerChannel"), "totalSamplesPerChannel missing in on-open meta.json");
        QCOMPARE(obj["totalSamplesPerChannel"].toInt(), 0); // Should be 0 before any writes

        recorder.close();
    }

    void doubleOpenReturnsFalse() {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());

        const int kSampleRate = 256;

        auto meta = SessionMetadata()
            .withSubjectId("S_DOUBLE")
            .withSampleRate(kSampleRate);

        QString basePath = tmpDir.filePath("rec_double");

        Recorder recorder;
        QVERIFY(recorder.open(basePath, meta));

        // Try to open again while already open
        bool result = recorder.open(basePath + "_2", meta);
        QVERIFY(!result);

        recorder.close();
    }
};

QTEST_MAIN(TestRecorder)
#include "test_recorder.moc"
