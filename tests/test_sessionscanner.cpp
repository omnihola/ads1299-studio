// tests/test_sessionscanner.cpp
//
// QtTest unit tests for studio::SessionScanner.
// Headless: QT_QPA_PLATFORM=offscreen (set in CMakeLists).

#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "core/recording/SessionScanner.h"

namespace studio {

class TestSessionScanner : public QObject
{
    Q_OBJECT

private:
    // Helper: write a meta.json that matches what Recorder::writeMetaJson produces.
    static void writeMeta(const QString& metaPath,
                          const QString& subjectId,
                          int            sampleRate,
                          qint64         totalSamples,
                          const QString& startTimeUtc)
    {
        QJsonObject obj;
        // Top-level keys as written by Recorder::writeMetaJson / SessionMetadata::toJson
        obj["subjectId"]               = subjectId;
        obj["sampleRate"]              = sampleRate;
        obj["totalSamplesPerChannel"]  = totalSamples;
        obj["recordingStartUtc"]       = startTimeUtc;
        obj["montage"]                 = "";
        obj["operatorNotes"]           = "";
        QFile f(metaPath);
        QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Text),
                 qPrintable("cannot write " + metaPath));
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        f.close();
    }

private slots:

    // ── parseExtractsFields ───────────────────────────────────────────────────

    void parseExtractsFields()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QString base     = tmp.path() + "/rec001";
        const QString metaPath = base + ".meta.json";

        writeMeta(metaPath, "Alice", 250, 5000, "2024-03-10T10:00:00.000Z");

        // Create the .bdf sidecar; no .csv
        QFile bdf(base + ".bdf");
        QVERIFY(bdf.open(QIODevice::WriteOnly));
        bdf.write("BDF");
        bdf.close();

        const SessionInfo info = SessionScanner::parseMeta(metaPath);

        QCOMPARE(info.subjectId,   QString("Alice"));
        QCOMPARE(info.sampleRate,  250);
        QCOMPARE(info.totalSamples, qint64(5000));
        // durationSec = 5000 / 250 = 20.0
        QCOMPARE(info.durationSec, 20.0);
        QVERIFY(info.hasBdf);
        QVERIFY(!info.hasCsv);
        QCOMPARE(info.startTimeUtc, QString("2024-03-10T10:00:00.000Z"));
        // basePath = metaPath without ".meta.json"
        QCOMPARE(info.basePath, base);
    }

    // ── scanFindsAllSortedNewestFirst ─────────────────────────────────────────

    void scanFindsAllSortedNewestFirst()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QString base1 = tmp.path() + "/older";
        const QString base2 = tmp.path() + "/newer";

        writeMeta(base1 + ".meta.json", "Bob",   250, 1000, "2024-01-01T08:00:00.000Z");
        writeMeta(base2 + ".meta.json", "Carol", 500, 2000, "2024-06-15T12:30:00.000Z");

        const QVector<SessionInfo> sessions = SessionScanner::scan(tmp.path());

        QCOMPARE(sessions.size(), 2);
        // Newest (2024-06-15) should be first
        QCOMPARE(sessions[0].subjectId, QString("Carol"));
        QCOMPARE(sessions[1].subjectId, QString("Bob"));
    }

    // ── scanSkipsUnparseable ──────────────────────────────────────────────────

    void scanSkipsUnparseable()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        writeMeta(tmp.path() + "/good1.meta.json", "Dave", 250, 100, "2024-02-01T00:00:00.000Z");
        writeMeta(tmp.path() + "/good2.meta.json", "Eve",  250, 200, "2024-02-02T00:00:00.000Z");

        // Non-JSON garbage
        QFile bad(tmp.path() + "/garbage.meta.json");
        QVERIFY(bad.open(QIODevice::WriteOnly | QIODevice::Text));
        bad.write("not json {{ !!!");
        bad.close();

        const QVector<SessionInfo> sessions = SessionScanner::scan(tmp.path());

        QCOMPARE(sessions.size(), 2);
    }

    // ── durationZeroWhenRateZero ──────────────────────────────────────────────

    void durationZeroWhenRateZero()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        const QString metaPath = tmp.path() + "/zero.meta.json";
        writeMeta(metaPath, "Frank", 0, 9999, "2024-03-01T00:00:00.000Z");

        const SessionInfo info = SessionScanner::parseMeta(metaPath);

        QCOMPARE(info.sampleRate,  0);
        QCOMPARE(info.durationSec, 0.0);
    }
};

} // namespace studio

QTEST_MAIN(studio::TestSessionScanner)
#include "test_sessionscanner.moc"
