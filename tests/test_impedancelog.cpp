#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include "core/recording/ImpedanceLog.h"

using namespace studio;

class TestImpedanceLog : public QObject {
    Q_OBJECT

private slots:

    // statusFor — exact threshold boundaries
    void statusFor_ok() {
        QCOMPARE(ImpedanceLog::statusFor(5.0, false), ImpedanceStatus::Ok);
    }

    void statusFor_warn() {
        QCOMPARE(ImpedanceLog::statusFor(30.0, false), ImpedanceStatus::Warn);
    }

    void statusFor_error_highKohm() {
        QCOMPARE(ImpedanceLog::statusFor(100.0, false), ImpedanceStatus::Error);
    }

    void statusFor_error_leadOff_lowKohm() {
        // Lead-off bit forces Error even at low kΩ
        QCOMPARE(ImpedanceLog::statusFor(2.0, true), ImpedanceStatus::Error);
    }

    void statusFor_boundary_10_is_warn() {
        QCOMPARE(ImpedanceLog::statusFor(10.0, false), ImpedanceStatus::Warn);
    }

    void statusFor_boundary_50_is_error() {
        QCOMPARE(ImpedanceLog::statusFor(50.0, false), ImpedanceStatus::Error);
    }

    // record / all / count / clear
    void recordAndCount() {
        ImpedanceLog log;
        QCOMPARE(log.count(), 0);

        std::array<double, 8> kohm{};
        kohm.fill(5.0);
        log.record(0.0, kohm, 0x00, 0x00);
        log.record(1.0, kohm, 0x01, 0x00);

        QCOMPARE(log.count(), 2);

        const QVector<ImpedanceSample> samples = log.all();
        QCOMPARE(samples.size(), 2);
        QCOMPARE(samples[0].tSec, 0.0);
        QCOMPARE(samples[1].tSec, 1.0);
        QCOMPARE(samples[1].statP, static_cast<uint8_t>(0x01));
    }

    void clearResetsCount() {
        ImpedanceLog log;
        std::array<double, 8> kohm{};
        kohm.fill(3.0);
        log.record(0.0, kohm, 0x00, 0x00);
        QCOMPARE(log.count(), 1);
        log.clear();
        QCOMPARE(log.count(), 0);
    }

    // toJson
    void toJson_twoSamples() {
        ImpedanceLog log;

        std::array<double, 8> kohm0{};
        kohm0.fill(7.0);
        std::array<double, 8> kohm1{};
        for (int i = 0; i < 8; ++i) kohm1[static_cast<size_t>(i)] = static_cast<double>(i + 1);

        log.record(0.5, kohm0, 0x00, 0x00);
        log.record(1.5, kohm1, 0x03, 0xFF);

        QJsonArray arr = log.toJson();
        QCOMPARE(arr.size(), 2);

        QJsonObject obj0 = arr[0].toObject();
        QVERIFY(obj0.contains("t"));
        QCOMPARE(obj0["t"].toDouble(), 0.5);
        QVERIFY(obj0.contains("kohm"));
        QCOMPARE(obj0["kohm"].toArray().size(), 8);
        QCOMPARE(obj0["kohm"].toArray()[0].toDouble(), 7.0);

        QJsonObject obj1 = arr[1].toObject();
        QCOMPARE(obj1["t"].toDouble(), 1.5);
        QCOMPARE(obj1["statP"].toInt(), 0x03);
        QCOMPARE(obj1["statN"].toInt(), 0xFF);
        QJsonArray kohmArr = obj1["kohm"].toArray();
        QCOMPARE(kohmArr.size(), 8);
        QCOMPARE(kohmArr[0].toDouble(), 1.0);
        QCOMPARE(kohmArr[7].toDouble(), 8.0);
    }

    // writeCsv
    void writeCsv_createsFileWithHeader() {
        ImpedanceLog log;
        std::array<double, 8> kohm{};
        for (int i = 0; i < 8; ++i) kohm[static_cast<size_t>(i)] = static_cast<double>(i + 1);
        log.record(0.0, kohm, 0xAB, 0xCD);

        QString path = QDir::tempPath() + "/test_impedancelog_writeCsv.csv";
        QVERIFY(log.writeCsv(path));

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        QTextStream ts(&f);
        QString header = ts.readLine();
        QCOMPARE(header, QString("t_sec,ch0_kohm,ch1_kohm,ch2_kohm,ch3_kohm,ch4_kohm,ch5_kohm,ch6_kohm,ch7_kohm,statP,statN"));
        QString dataLine = ts.readLine();
        QVERIFY(!dataLine.isEmpty());
        f.close();
        QFile::remove(path);
    }

    void writeCsv_badPathReturnsFalse() {
        ImpedanceLog log;
        QVERIFY(!log.writeCsv("/nonexistent_dir_xyz/out.csv"));
    }
};

QTEST_MAIN(TestImpedanceLog)
#include "test_impedancelog.moc"
