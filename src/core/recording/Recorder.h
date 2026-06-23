#pragma once

#include <QFile>
#include <QObject>
#include <QString>
#include <QTextStream>

#include "core/acquisition/EegFrame.h"
#include "core/recording/SessionMetadata.h"

namespace studio {

/**
 * Recorder — writes incoming EEG frames to BDF+ (via EDFlib) and CSV in
 * parallel.
 *
 * Lifecycle:
 *   1. Recorder recorder;
 *   2. recorder.open(basePath, meta);    // creates .bdf, .csv, .meta.json
 *   3. recorder.writeBatch(batch);       // call as many times as needed
 *   4. recorder.close();                 // flushes & finalises all three files
 *
 * BDF+ details
 * ============
 * - Format : EDFLIB_FILETYPE_BDFPLUS (3)  — 24-bit two's-complement
 * - Signals : 8, one per EEG channel
 * - Data records : 1-second boundaries (sampleRate samples per record)
 * - If close() is called mid-record the partial record is zero-padded to a
 *   full record before being written, and the exact sample count is stored in
 *   meta.json.
 * - Digital range  : [-8388608, 8388607]
 * - Physical range : derived from ScaleConverter for each channel's gain
 *
 * CSV details
 * ===========
 * Header: seq,t_seconds,ch0_uV,ch1_uV,...,ch7_uV
 * One row per EegFrame. t_seconds = frameIndex / sampleRate. Values in µV via
 * ScaleConverter.
 *
 * meta.json
 * =========
 * Written on open (draft) and rewritten on close (with totalSamplesPerChannel,
 * bdfFile, csvFile added).
 *
 * Thread safety
 * =============
 * Not thread-safe. Call from a single thread.
 */
class Recorder : public QObject
{
    Q_OBJECT

public:
    explicit Recorder(QObject* parent = nullptr);
    ~Recorder() override;

    /**
     * Opens the recording at \p basePath (no extension).
     * Creates basePath.bdf, basePath.csv, and basePath.meta.json.
     * @return true on success, false on error (emits errorOccurred).
     */
    bool open(const QString& basePath, const SessionMetadata& meta);

    /**
     * Appends all frames in \p batch to the recording.
     * Does nothing and logs silently if the recorder is not open or in error.
     */
    void writeBatch(const EegFrameBatch& batch);

    /**
     * Finalises the BDF+ file, flushes and closes the CSV, rewrites meta.json
     * with final statistics.
     */
    void close();

    bool    isOpen()         const { return isOpen_; }
    quint64 samplesWritten() const { return samplesWritten_; }

signals:
    void errorOccurred(const QString& message);

private:
    // Internal helpers
    void flushOneRecord();   // writes one full BDF+ data record from buf_
    void writeCsvRow(const EegFrame& frame, quint64 frameIndex);
    void writeMetaJson(bool isFinal);
    void handleEdfError(const QString& context, int code);

    // State
    bool     isOpen_     = false;
    bool     hasError_   = false;
    int      edfHandle_  = -1;
    quint64  samplesWritten_ = 0;

    // Session info
    SessionMetadata meta_;
    QString         basePath_;
    int             sampleRate_    = 0;
    static constexpr int kChannels = 8;

    // Per-record buffer: interleaved layout
    // [ch0 sample0..sampleN-1] [ch1 sample0..sampleN-1] ... [ch7 ...]
    // Size = kChannels * sampleRate_
    QVector<int> recBuf_;
    int          recBufFilled_ = 0; // number of complete samples per channel

    // CSV
    QFile        csvFile_;
    QTextStream  csvStream_;
};

} // namespace studio
