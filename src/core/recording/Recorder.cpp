#include "core/recording/Recorder.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTextStream>

#include "core/dsp/ScaleConverter.h"
#include "core/logging/Logger.h"

extern "C" {
#include "edflib.h"
}

namespace studio {

// ─── Construction / destruction ──────────────────────────────────────────────

Recorder::Recorder(QObject* parent)
    : QObject(parent)
{}

Recorder::~Recorder()
{
    if (isOpen_) {
        close();
    }
}

// ─── open ────────────────────────────────────────────────────────────────────

bool Recorder::open(const QString& basePath, const SessionMetadata& meta)
{
    if (basePath.isEmpty()) {
        emit errorOccurred("Recorder::open — basePath is empty");
        return false;
    }

    if (meta.sampleRate() <= 0) {
        emit errorOccurred("Recorder::open — sampleRate must be > 0");
        return false;
    }

    if (isOpen_) {
        emit errorOccurred("Recorder::open — recorder is already open");
        Logger::instance().log("error", "recorder.open.already_open", {});
        return false;
    }

    meta_        = meta;
    basePath_    = basePath;
    sampleRate_  = meta.sampleRate();
    hasError_        = false;
    samplesWritten_  = 0;
    paddedSamples_   = 0;
    timelineIndex_   = 0;
    annotations_.clear();

    // Parse recording start time from metadata; fall back to current UTC time.
    recordingStart_ = QDateTime::fromString(meta.startTimeUtc(), Qt::ISODateWithMs);
    if (!recordingStart_.isValid()) {
        recordingStart_ = QDateTime::currentDateTimeUtc();
    }

    // Build per-channel ScaleConverters using the session gain settings.
    const auto gains = meta.gainPerChannel();
    for (int ch = 0; ch < kChannels; ++ch) {
        converters_[ch] = ScaleConverter(gains[ch]);
    }

    // ── 1. Open BDF+ file ────────────────────────────────────────────────────
    QString bdfPath = basePath_ + ".bdf";
    edfHandle_ = edfopen_file_writeonly(
        bdfPath.toUtf8().constData(),
        EDFLIB_FILETYPE_BDFPLUS,
        kChannels);

    if (edfHandle_ < 0) {
        QString msg = QString("Recorder::open — edfopen_file_writeonly failed (code %1)").arg(edfHandle_);
        emit errorOccurred(msg);
        Logger::instance().log("error", "recorder.open.edf_open_failed",
                               {{"code", edfHandle_}, {"path", bdfPath}});
        return false;
    }

    // ── 2. Configure each signal ─────────────────────────────────────────────
    // Note: converters_[] are already built above; reuse them for physical range.
    for (int ch = 0; ch < kChannels; ++ch) {
        const ScaleConverter& sc = converters_[ch];

        // BDF digital range: 24-bit two's-complement
        constexpr int kDigMax =  8388607;
        constexpr int kDigMin = -8388608;

        double physMaxUv = sc.countsToMicrovolts(kDigMax);
        double physMinUv = sc.countsToMicrovolts(kDigMin);

        // Signal label — max 16 chars in EDF spec
        char label[17];
        std::snprintf(label, sizeof(label), "EEG CH%d", ch + 1);

        int r = 0;

        r = edf_set_samplefrequency(edfHandle_, ch, sampleRate_);
        if (r < 0) { handleEdfError(QString("edf_set_samplefrequency ch%1").arg(ch), r); return false; }

        r = edf_set_digital_maximum(edfHandle_, ch, kDigMax);
        if (r < 0) { handleEdfError(QString("edf_set_digital_maximum ch%1").arg(ch), r); return false; }

        r = edf_set_digital_minimum(edfHandle_, ch, kDigMin);
        if (r < 0) { handleEdfError(QString("edf_set_digital_minimum ch%1").arg(ch), r); return false; }

        r = edf_set_physical_maximum(edfHandle_, ch, physMaxUv);
        if (r < 0) { handleEdfError(QString("edf_set_physical_maximum ch%1").arg(ch), r); return false; }

        r = edf_set_physical_minimum(edfHandle_, ch, physMinUv);
        if (r < 0) { handleEdfError(QString("edf_set_physical_minimum ch%1").arg(ch), r); return false; }

        r = edf_set_physical_dimension(edfHandle_, ch, "uV");
        if (r < 0) { handleEdfError(QString("edf_set_physical_dimension ch%1").arg(ch), r); return false; }

        r = edf_set_label(edfHandle_, ch, label);
        if (r < 0) { handleEdfError(QString("edf_set_label ch%1").arg(ch), r); return false; }
    }

    // Set patient name from subjectId (optional but useful)
    if (!meta_.subjectId().isEmpty()) {
        edf_set_patientname(edfHandle_, meta_.subjectId().toUtf8().constData());
    }

    // ── 3. Allocate per-record buffer ─────────────────────────────────────────
    recBuf_.resize(kChannels * sampleRate_);
    recBuf_.fill(0);
    recBufFilled_ = 0;

    // ── 4. Open CSV ───────────────────────────────────────────────────────────
    QString csvPath = basePath_ + ".csv";
    csvFile_.setFileName(csvPath);
    if (!csvFile_.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QString msg = QString("Recorder::open — cannot open CSV: %1").arg(csvPath);
        edfclose_file(edfHandle_);
        edfHandle_ = -1;
        emit errorOccurred(msg);
        Logger::instance().log("error", "recorder.open.csv_open_failed", {{"path", csvPath}});
        return false;
    }
    csvStream_.setDevice(&csvFile_);

    // Write CSV header (research-grade: absolute timestamp, raw counts, µV, status, flag)
    csvStream_ << "timestamp_utc,seq,t_seconds,statP,statN,gpio";
    for (int c = 0; c < kChannels; ++c) {
        csvStream_ << ",ch" << c << "_raw";
    }
    for (int c = 0; c < kChannels; ++c) {
        csvStream_ << ",ch" << c << "_uV";
    }
    csvStream_ << ",flag\n";

    // ── 5. Write initial meta.json ────────────────────────────────────────────
    writeMetaJson(false);

    isOpen_ = true;
    return true;
}

// ─── addAnnotation ───────────────────────────────────────────────────────────

void Recorder::addAnnotation(double onsetSec, const QString& label)
{
    annotations_.add(onsetSec, label);
}

// ─── writeBatch ──────────────────────────────────────────────────────────────

void Recorder::writeBatch(const EegFrameBatch& batch)
{
    if (!isOpen_ || hasError_) return;

    for (const EegFrame& frame : batch) {
        // ── Accumulate into per-record buffer ─────────────────────────────────
        for (int ch = 0; ch < kChannels; ++ch) {
            // Buffer layout: [ch0 block][ch1 block]...[ch7 block]
            // Each block is sampleRate_ ints.
            recBuf_[ch * sampleRate_ + recBufFilled_] = frame.ch[ch];
        }
        ++recBufFilled_;

        if (recBufFilled_ == sampleRate_) {
            flushOneRecord();
            if (hasError_) return;
        }

        // ── CSV row ───────────────────────────────────────────────────────────
        writeCsvRow(frame, timelineIndex_);
        ++timelineIndex_;

        ++samplesWritten_;
    }
}

// ─── writeGap ────────────────────────────────────────────────────────────────

void Recorder::writeGap(quint32 nSamples)
{
    if (!isOpen_ || hasError_ || nSamples == 0) return;

    for (quint32 i = 0; i < nSamples; ++i) {
        // Write a zero sample into every channel in the record buffer.
        for (int ch = 0; ch < kChannels; ++ch) {
            recBuf_[ch * sampleRate_ + recBufFilled_] = 0;
        }
        ++recBufFilled_;

        if (recBufFilled_ == sampleRate_) {
            flushOneRecord();
            if (hasError_) return;
        }

        // Write a zero-fill CSV row so CSV row count stays aligned with BDF.
        writeGapCsvRow(timelineIndex_);
        ++timelineIndex_;

        ++paddedSamples_;
    }
}

// ─── close ───────────────────────────────────────────────────────────────────

void Recorder::close()
{
    if (!isOpen_) return;

    // Pad & flush partial record (if any)
    if (!hasError_ && recBufFilled_ > 0) {
        // Zero-pad remaining positions
        for (int ch = 0; ch < kChannels; ++ch) {
            for (int s = recBufFilled_; s < sampleRate_; ++s) {
                recBuf_[ch * sampleRate_ + s] = 0;
            }
        }
        flushOneRecord();
    }

    // Write annotations (event markers) into the BDF+ before closing.
    if (edfHandle_ >= 0) {
        annotations_.writeToBdf(edfHandle_);
        edfclose_file(edfHandle_);
        edfHandle_ = -1;
    }

    // Flush & close CSV
    csvStream_.flush();
    csvFile_.close();

    // Rewrite meta.json with final stats
    writeMetaJson(true);

    isOpen_ = false;
}

// ─── flushOneRecord ──────────────────────────────────────────────────────────

void Recorder::flushOneRecord()
{
    // edf_blockwrite_digital_samples: writes one full data record.
    // Buffer layout must be: [n samples of sig0][n samples of sig1]...[n samples of sigN-1]
    // n = samplefrequency per signal = sampleRate_
    // For BDF+, the 24 least-significant bits of each int are written.
    int r = edf_blockwrite_digital_samples(edfHandle_, recBuf_.data());
    if (r < 0) {
        handleEdfError("edf_blockwrite_digital_samples", r);
        return;
    }

    recBufFilled_ = 0;
    recBuf_.fill(0);
}

// ─── writeCsvRow ─────────────────────────────────────────────────────────────

void Recorder::writeCsvRow(const EegFrame& frame, quint64 timelineIdx)
{
    const double tSeconds = static_cast<double>(timelineIdx) / static_cast<double>(sampleRate_);

    // Compute absolute UTC timestamp for this sample.
    const qint64 msecOffset = static_cast<qint64>(
        static_cast<double>(timelineIdx) * 1000.0 / static_cast<double>(sampleRate_));
    const QString tsUtc = recordingStart_.addMSecs(msecOffset)
                              .toUTC()
                              .toString("yyyy-MM-ddThh:mm:ss.zzzZ");

    // timestamp_utc, seq, t_seconds, statP, statN, gpio
    csvStream_ << tsUtc
               << "," << frame.seq
               << "," << QString::number(tSeconds, 'f', 9)
               << "," << static_cast<quint32>(frame.statP)
               << "," << static_cast<quint32>(frame.statN)
               << "," << static_cast<quint32>(frame.gpio);

    // ch0_raw..ch7_raw
    for (int c = 0; c < kChannels; ++c) {
        csvStream_ << "," << frame.ch[c];
    }

    // ch0_uV..ch7_uV
    for (int c = 0; c < kChannels; ++c) {
        csvStream_ << "," << converters_[c].countsToMicrovolts(frame.ch[c]);
    }

    // flag (empty for real frames)
    csvStream_ << ",\n";

    // Flush every 1000 rows to avoid large write bursts
    if ((timelineIdx % 1000) == 0) {
        csvStream_.flush();
    }
}

// ─── writeGapCsvRow ──────────────────────────────────────────────────────────

void Recorder::writeGapCsvRow(quint64 timelineIdx)
{
    // Gap rows: seq/statP/statN/gpio blank, all channel counts 0, flag "drop_pad".
    const double tSeconds = static_cast<double>(timelineIdx) / static_cast<double>(sampleRate_);

    const qint64 msecOffset = static_cast<qint64>(
        static_cast<double>(timelineIdx) * 1000.0 / static_cast<double>(sampleRate_));
    const QString tsUtc = recordingStart_.addMSecs(msecOffset)
                              .toUTC()
                              .toString("yyyy-MM-ddThh:mm:ss.zzzZ");

    // timestamp_utc, blank seq, t_seconds, blank statP, blank statN, blank gpio
    csvStream_ << tsUtc
               << ","   // blank seq
               << "," << QString::number(tSeconds, 'f', 9)
               << ","   // blank statP
               << ","   // blank statN
               << ",";  // blank gpio (comma separates from next field)

    // ch0_raw..ch7_raw — all zero
    for (int c = 0; c < kChannels; ++c) {
        csvStream_ << ",0";
    }

    // ch0_uV..ch7_uV — all 0.0
    for (int c = 0; c < kChannels; ++c) {
        csvStream_ << ",0";
    }

    // flag
    csvStream_ << ",drop_pad\n";
}

// ─── writeMetaJson ───────────────────────────────────────────────────────────

void Recorder::writeMetaJson(bool isFinal)
{
    Q_UNUSED(isFinal)
    QJsonObject obj = meta_.toJson();

    obj["bdfFile"] = QFileInfo(basePath_ + ".bdf").fileName();
    obj["csvFile"] = QFileInfo(basePath_ + ".csv").fileName();
    obj["totalSamplesPerChannel"] = static_cast<qint64>(samplesWritten_);
    obj["paddedSamples"]          = static_cast<qint64>(paddedSamples_);
    obj["annotations"] = annotations_.toJson();

    // Phase-9 enrichment: absolute start time, sample rate, per-channel gain, CSV columns
    obj["recordingStartUtc"] = recordingStart_.toUTC().toString(Qt::ISODateWithMs);
    obj["sampleRate"]        = meta_.sampleRate();

    QJsonArray gainArr;
    const auto gains = meta_.gainPerChannel();
    for (int ch = 0; ch < kChannels; ++ch) {
        gainArr.append(gains[ch]);
    }
    obj["gainPerChannel"] = gainArr;

    static const QJsonArray kCsvColumns = QJsonArray{
        "timestamp_utc", "seq", "t_seconds",
        "statP", "statN", "gpio",
        "ch0_raw", "ch1_raw", "ch2_raw", "ch3_raw",
        "ch4_raw", "ch5_raw", "ch6_raw", "ch7_raw",
        "ch0_uV",  "ch1_uV",  "ch2_uV",  "ch3_uV",
        "ch4_uV",  "ch5_uV",  "ch6_uV",  "ch7_uV",
        "flag"
    };
    obj["csvColumns"] = kCsvColumns;

    QString metaPath = basePath_ + ".meta.json";
    QFile f(metaPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        Logger::instance().log("error", "recorder.meta_json_write_failed",
                               {{"path", metaPath}});
        return;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

// ─── handleEdfError ──────────────────────────────────────────────────────────

void Recorder::handleEdfError(const QString& context, int code)
{
    hasError_ = true;
    QString msg = QString("Recorder EDFlib error in %1 (code %2)").arg(context).arg(code);
    emit errorOccurred(msg);
    Logger::instance().log("error", "recorder.edf_error",
                           {{"context", context}, {"code", code}});
}

} // namespace studio
