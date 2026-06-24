// tests/test_rawcsv.cpp
// TDD Phase 9 Task 1 — research-grade fidelity CSV
// Verifies that recorded CSV contains: absolute UTC timestamp, seq, t_seconds,
// statP/statN/gpio, raw counts (int32), µV (double), flag.
// Also verifies meta.json enrichment.

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QRegularExpression>

#include "core/recording/Recorder.h"
#include "core/recording/SessionMetadata.h"
#include "core/acquisition/EegFrame.h"

using namespace studio;

// ---------------------------------------------------------------------------
// Helper: parse a CSV file into a list of rows (each row = QStringList of fields)
// ---------------------------------------------------------------------------
static QVector<QStringList> parseCsv(const QString& path)
{
    QVector<QStringList> rows;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return rows;
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        rows.append(line.split(','));
    }
    return rows;
}

class TestRawCsv : public QObject
{
    Q_OBJECT

private slots:

    void completePerSampleRawRecord()
    {
        // ----------------------------------------------------------------
        // Arrange
        // ----------------------------------------------------------------
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());

        const int kSampleRate = 256;

        // All gains = 24
        std::array<int, 8> gains;
        gains.fill(24);

        auto meta = SessionMetadata()
            .withSubjectId("RawCsvTest")
            .withSampleRate(kSampleRate)
            .withGainPerChannel(gains);

        const QString basePath = tmpDir.filePath("raw_rec");

        // Helper lambda to build a test frame
        auto makeFrame = [](uint32_t seq) -> EegFrame {
            EegFrame f{};
            f.seq   = seq;
            f.statP = 0xA5;   // 165
            f.statN = 0x3C;   // 60
            f.gpio  = 0x0F;   // 15
            f.ch[0] = +1000;
            f.ch[1] = -2000;
            // ch2..7 = c * 100
            for (int c = 2; c < 8; ++c) {
                f.ch[c] = c * 100;
            }
            return f;
        };

        // ----------------------------------------------------------------
        // Act — write 3 real frames, a 2-sample gap, then 1 real frame
        // ----------------------------------------------------------------
        Recorder recorder;
        QVERIFY(recorder.open(basePath, meta));

        // frames seq=10,11,12
        EegFrameBatch batch1;
        batch1.push_back(makeFrame(10));
        batch1.push_back(makeFrame(11));
        batch1.push_back(makeFrame(12));
        recorder.writeBatch(batch1);

        recorder.writeGap(2);

        // frame seq=13
        EegFrameBatch batch2;
        batch2.push_back(makeFrame(13));
        recorder.writeBatch(batch2);

        recorder.close();

        // ----------------------------------------------------------------
        // Load CSV
        // ----------------------------------------------------------------
        const QString csvPath = basePath + ".csv";
        QVERIFY2(QFile::exists(csvPath), "CSV file must exist");

        const QVector<QStringList> rows = parseCsv(csvPath);
        // rows[0] = header, rows[1..6] = data (3 real + 2 pad + 1 real)
        QVERIFY2(rows.size() == 7, qPrintable(QString("Expected 7 lines (1 header + 6 data), got %1").arg(rows.size())));

        // ----------------------------------------------------------------
        // 1. Header check
        // ----------------------------------------------------------------
        const QStringList& header = rows[0];
        const QString headerStr = header.join(',');
        const QString expectedHeader =
            "timestamp_utc,seq,t_seconds,statP,statN,gpio,"
            "ch0_raw,ch1_raw,ch2_raw,ch3_raw,ch4_raw,ch5_raw,ch6_raw,ch7_raw,"
            "ch0_uV,ch1_uV,ch2_uV,ch3_uV,ch4_uV,ch5_uV,ch6_uV,ch7_uV,flag";
        QCOMPARE(headerStr, expectedHeader);

        // ----------------------------------------------------------------
        // Helper: column index by name
        // ----------------------------------------------------------------
        auto colIdx = [&](const QString& name) -> int {
            return header.indexOf(name);
        };

        const int iTimestamp = colIdx("timestamp_utc");
        const int iSeq       = colIdx("seq");
        const int iTsec      = colIdx("t_seconds");
        const int iStatP     = colIdx("statP");
        const int iStatN     = colIdx("statN");
        const int iGpio      = colIdx("gpio");
        const int iCh0raw    = colIdx("ch0_raw");
        const int iCh1raw    = colIdx("ch1_raw");
        const int iCh0uV     = colIdx("ch0_uV");
        const int iFlag      = colIdx("flag");

        QVERIFY(iTimestamp >= 0);
        QVERIFY(iSeq       >= 0);
        QVERIFY(iTsec      >= 0);
        QVERIFY(iStatP     >= 0);
        QVERIFY(iStatN     >= 0);
        QVERIFY(iGpio      >= 0);
        QVERIFY(iCh0raw    >= 0);
        QVERIFY(iCh1raw    >= 0);
        QVERIFY(iCh0uV     >= 0);
        QVERIFY(iFlag      >= 0);

        // ----------------------------------------------------------------
        // 2. First data row (rows[1], timeline index 0)
        // ----------------------------------------------------------------
        const QStringList& row0 = rows[1];

        // timestamp_utc format
        const QRegularExpression tsRx(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$)");
        QVERIFY2(tsRx.match(row0[iTimestamp]).hasMatch(),
                 qPrintable(QString("timestamp_utc '%1' does not match ISO 8601 pattern").arg(row0[iTimestamp])));

        // seq
        QCOMPARE(row0[iSeq], QString("10"));

        // statP, statN, gpio
        QCOMPARE(row0[iStatP], QString("165"));
        QCOMPARE(row0[iStatN], QString("60"));
        QCOMPARE(row0[iGpio],  QString("15"));

        // ch0_raw, ch1_raw
        QCOMPARE(row0[iCh0raw], QString("1000"));
        QCOMPARE(row0[iCh1raw], QString("-2000"));

        // ch0_uV approximately = 1000 * (4.5 / (24 * 8388608.0)) * 1e6
        const double kVref    = 4.5;
        const double kGain    = 24.0;
        const double kFullScale = 8388608.0;
        const double expectedUv = 1000.0 * (kVref / (kGain * kFullScale)) * 1e6;

        bool okUv = false;
        const double gotUv = row0[iCh0uV].toDouble(&okUv);
        QVERIFY2(okUv, "ch0_uV must be parseable as double");
        QVERIFY2(std::abs(gotUv - expectedUv) < 1e-3,
                 qPrintable(QString("ch0_uV: expected ~%1, got %2").arg(expectedUv).arg(gotUv)));

        // flag: empty for real frames
        QCOMPARE(row0[iFlag], QString(""));

        // ----------------------------------------------------------------
        // 3. Exactly 2 rows have flag == "drop_pad"
        // ----------------------------------------------------------------
        int dropPadCount = 0;
        for (int r = 1; r < rows.size(); ++r) {
            const QStringList& row = rows[r];
            if (row.size() > iFlag && row[iFlag] == "drop_pad") {
                // blank seq
                QVERIFY2(row[iSeq].isEmpty(),
                         qPrintable(QString("row %1: drop_pad row must have blank seq").arg(r)));
                // ch0_raw == "0"
                QCOMPARE(row[iCh0raw], QString("0"));
                ++dropPadCount;
            }
        }
        QCOMPARE(dropPadCount, 2);

        // ----------------------------------------------------------------
        // 4. t_seconds increases by 1/sampleRate each row monotonically
        // ----------------------------------------------------------------
        const double kDt = 1.0 / static_cast<double>(kSampleRate);
        for (int r = 1; r < rows.size(); ++r) {
            const QStringList& row = rows[r];
            QVERIFY2(row.size() > iTsec, qPrintable(QString("row %1 too short").arg(r)));
            bool ok = false;
            const double tSec = row[iTsec].toDouble(&ok);
            QVERIFY2(ok, qPrintable(QString("row %1 t_seconds not parseable").arg(r)));

            const double expected = static_cast<double>(r - 1) * kDt;
            QVERIFY2(std::abs(tSec - expected) < 1e-9,
                     qPrintable(QString("row %1: t_seconds expected %2, got %3")
                                .arg(r).arg(expected, 0, 'f', 9).arg(tSec, 0, 'f', 9)));
        }

        // ----------------------------------------------------------------
        // 5. Total rows = 6 data rows (3 real + 2 pad + 1 real)
        // ----------------------------------------------------------------
        QCOMPARE(rows.size() - 1, 6);

        // ----------------------------------------------------------------
        // 6. meta.json enrichment
        // ----------------------------------------------------------------
        const QString metaPath = basePath + ".meta.json";
        QVERIFY2(QFile::exists(metaPath), "meta.json must exist");

        QFile metaFile(metaPath);
        QVERIFY(metaFile.open(QIODevice::ReadOnly));
        const auto doc = QJsonDocument::fromJson(metaFile.readAll());
        QVERIFY2(!doc.isNull(), "meta.json must be valid JSON");

        const QJsonObject obj = doc.object();

        // recordingStartUtc present
        QVERIFY2(obj.contains("recordingStartUtc"),
                 "meta.json must contain 'recordingStartUtc'");

        // sampleRate == 256
        QVERIFY2(obj.contains("sampleRate"),
                 "meta.json must contain 'sampleRate'");
        QCOMPARE(obj["sampleRate"].toInt(), kSampleRate);

        // gainPerChannel array of length 8
        QVERIFY2(obj.contains("gainPerChannel"),
                 "meta.json must contain 'gainPerChannel'");
        const QJsonArray gainArr = obj["gainPerChannel"].toArray();
        QCOMPARE(gainArr.size(), 8);
        for (int i = 0; i < 8; ++i) {
            QCOMPARE(gainArr[i].toInt(), 24);
        }

        // csvColumns array, first element "timestamp_utc"
        QVERIFY2(obj.contains("csvColumns"),
                 "meta.json must contain 'csvColumns'");
        const QJsonArray csvCols = obj["csvColumns"].toArray();
        QVERIFY2(!csvCols.isEmpty(), "csvColumns must not be empty");
        QCOMPARE(csvCols[0].toString(), QString("timestamp_utc"));
    }
};

QTEST_MAIN(TestRawCsv)
#include "test_rawcsv.moc"
