// tests/test_sessionexporter.cpp
//
// QtTest unit tests for studio::SessionExporter.

#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/recording/SessionExporter.h"

namespace studio {

class TestSessionExporter : public QObject
{
    Q_OBJECT

private slots:
    // ── Helpers ──────────────────────────────────────────────────────────────

    // Write `content` bytes to `path`, creating any necessary parent dirs.
    static void writeFile(const QString& path, const QByteArray& content)
    {
        QFile f(path);
        QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
        f.write(content);
        f.close();
    }

    // ── Test cases ───────────────────────────────────────────────────────────

    void relatedFilesFindsExisting()
    {
        QTemporaryDir srcDir;
        QVERIFY(srcDir.isValid());

        const QString base = srcDir.path() + "/sess";
        writeFile(base + ".bdf",       QByteArray("bdf-data"));
        writeFile(base + ".csv",       QByteArray("csv-data"));
        writeFile(base + ".meta.json", QByteArray("{\"v\":1}"));
        // intentionally NOT creating .impedance.csv

        const QStringList files = SessionExporter::relatedFiles(base);
        QCOMPARE(files.size(), 3);
        QVERIFY(files.contains(base + ".bdf"));
        QVERIFY(files.contains(base + ".csv"));
        QVERIFY(files.contains(base + ".meta.json"));
        QVERIFY(!files.contains(base + ".impedance.csv"));
    }

    void exportCopiesAll()
    {
        QTemporaryDir srcDir, dstDir;
        QVERIFY(srcDir.isValid());
        QVERIFY(dstDir.isValid());

        const QString base = srcDir.path() + "/sess";
        writeFile(base + ".bdf",       QByteArray("bdf-payload"));
        writeFile(base + ".csv",       QByteArray("csv-payload"));
        writeFile(base + ".meta.json", QByteArray("{\"sampleRate\":250}"));

        QStringList copied;
        QString error;
        const bool ok = SessionExporter::exportTo(base, dstDir.path(), copied, error);

        QVERIFY2(ok, qPrintable(error));
        QCOMPARE(copied.size(), 3);

        // All three destination files must exist and have the same size as the source.
        const QStringList names{".bdf", ".csv", ".meta.json"};
        for (const QString& ext : names) {
            const QString srcPath = base + ext;
            const QString dstPath = dstDir.path() + "/sess" + ext;
            QVERIFY2(QFile::exists(dstPath), qPrintable(dstPath + " missing"));
            QCOMPARE(QFileInfo(dstPath).size(), QFileInfo(srcPath).size());
        }
    }

    void exportNoFilesFails()
    {
        QTemporaryDir srcDir, dstDir;
        QVERIFY(srcDir.isValid());
        QVERIFY(dstDir.isValid());

        // No files created — nonexistent base path
        const QString base = srcDir.path() + "/nonexistent";

        QStringList copied;
        QString error;
        const bool ok = SessionExporter::exportTo(base, dstDir.path(), copied, error);

        QVERIFY(!ok);
        QVERIFY(!error.isEmpty());
    }

    // A mid-export failure must leave NO partial files behind (all-or-nothing).
    void exportRollsBackOnPartialFailure()
    {
        QTemporaryDir srcDir, dstDir;
        QVERIFY(srcDir.isValid());
        QVERIFY(dstDir.isValid());

        const QString base = srcDir.path() + "/sess";
        writeFile(base + ".bdf", QByteArray("bdf-payload"));
        writeFile(base + ".csv", QByteArray("csv-payload"));

        // Force the .csv copy to fail (after .bdf already copies) by placing a
        // DIRECTORY where the dest .csv file should go — QFile::remove/copy fails on it.
        QVERIFY(QDir(dstDir.path()).mkdir("sess.csv"));

        QStringList copied;
        QString error;
        const bool ok = SessionExporter::exportTo(base, dstDir.path(), copied, error);

        QVERIFY2(!ok, "export must fail when a destination file cannot be written");
        QVERIFY(!error.isEmpty());
        // The .bdf copied before the failure must have been rolled back.
        QVERIFY2(!QFile::exists(dstDir.path() + "/sess.bdf"),
                 "partial export must be rolled back (no leftover .bdf)");
        QVERIFY(copied.isEmpty());
    }

    void exportOverwrites()
    {
        QTemporaryDir srcDir, dstDir;
        QVERIFY(srcDir.isValid());
        QVERIFY(dstDir.isValid());

        const QString base = srcDir.path() + "/sess";
        const QByteArray srcContent("fresh-content-from-source");
        writeFile(base + ".bdf",       srcContent);
        writeFile(base + ".csv",       QByteArray("csv-data"));
        writeFile(base + ".meta.json", QByteArray("{\"v\":2}"));

        // Pre-populate destination with stale content
        const QString staleCsvPath = dstDir.path() + "/sess.csv";
        writeFile(staleCsvPath, QByteArray("old-stale-content"));

        QStringList copied;
        QString error;
        const bool ok = SessionExporter::exportTo(base, dstDir.path(), copied, error);

        QVERIFY2(ok, qPrintable(error));

        // The destination .csv must now match the source, not the stale content.
        QFile f(staleCsvPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray actual = f.readAll();
        f.close();
        QCOMPARE(actual, QByteArray("csv-data"));
    }
};

} // namespace studio

QTEST_MAIN(studio::TestSessionExporter)
#include "test_sessionexporter.moc"
