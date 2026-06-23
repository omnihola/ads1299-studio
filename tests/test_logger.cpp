#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/logging/Logger.h"

using namespace studio;

class TestLogger : public QObject
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        Logger::instance().close();
    }

    void test_singleLog_writesOneValidJsonlLine()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("test.jsonl");

        Logger::instance().open(path);
        Logger::instance().log("info", "connect", QJsonObject{{"port", "ttyUSB0"}});
        Logger::instance().close();

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        QByteArray content = f.readAll();
        f.close();

        // Split lines, filter empty trailing newline
        QList<QByteArray> lines;
        for (const QByteArray &line : content.split('\n')) {
            if (!line.trimmed().isEmpty())
                lines.append(line);
        }

        QCOMPARE(lines.size(), 1);

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(lines[0], &err);
        QCOMPARE(err.error, QJsonParseError::NoError);
        QVERIFY(doc.isObject());

        QJsonObject obj = doc.object();
        QCOMPARE(obj["event"].toString(), QString("connect"));
        QCOMPARE(obj["level"].toString(), QString("info"));
        QCOMPARE(obj["port"].toString(), QString("ttyUSB0"));
        QVERIFY(obj.contains("ts"));
        QVERIFY(!obj["ts"].toString().isEmpty());
    }

    void test_twoLogs_writeTwoLines()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("test2.jsonl");

        Logger::instance().open(path);
        Logger::instance().log("info", "record_start", {});
        Logger::instance().log("warn", "drop", QJsonObject{{"samples", "3"}});
        Logger::instance().close();

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        QByteArray content = f.readAll();
        f.close();

        QList<QByteArray> lines;
        for (const QByteArray &line : content.split('\n')) {
            if (!line.trimmed().isEmpty())
                lines.append(line);
        }

        QCOMPARE(lines.size(), 2);

        QJsonDocument doc1 = QJsonDocument::fromJson(lines[0]);
        QVERIFY(doc1.isObject());
        QCOMPARE(doc1.object()["event"].toString(), QString("record_start"));

        QJsonDocument doc2 = QJsonDocument::fromJson(lines[1]);
        QVERIFY(doc2.isObject());
        QCOMPARE(doc2.object()["event"].toString(), QString("drop"));
    }

    void test_reservedKeys_protectedFromSpoofing()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("test3.jsonl");

        Logger::instance().open(path);
        Logger::instance().log("info", "record_start", QJsonObject{{"event", "SPOOFED"}, {"subject", "s1"}});
        Logger::instance().close();

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        QByteArray content = f.readAll();
        f.close();

        QList<QByteArray> lines;
        for (const QByteArray &line : content.split('\n')) {
            if (!line.trimmed().isEmpty())
                lines.append(line);
        }

        QCOMPARE(lines.size(), 1);

        QJsonDocument doc = QJsonDocument::fromJson(lines[0]);
        QVERIFY(doc.isObject());
        QJsonObject obj = doc.object();

        // Reserved fields must not be overwritten by caller-supplied fields
        QCOMPARE(obj["event"].toString(), QString("record_start"));
        QCOMPARE(obj["level"].toString(), QString("info"));
        // But caller fields should still be present
        QCOMPARE(obj["subject"].toString(), QString("s1"));
    }
};

QTEST_MAIN(TestLogger)
#include "test_logger.moc"
