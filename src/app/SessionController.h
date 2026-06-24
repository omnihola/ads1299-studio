#pragma once
// src/app/SessionController.h
//
// SessionController owns an IDataSource and moves it to a worker QThread.
// It connects source->framesReady to onFrames() via a queued connection so
// the frame handler always executes on the *controller's* thread (typically
// the GUI thread), keeping droppedSamples_ and expectedSeq_ single-threaded.
//
// Thread-safety note:
//   droppedSamples_ is std::atomic<uint64_t> so the GUI thread can safely
//   read it via droppedSamples() while onFrames() (on the controller thread)
//   updates it.  expectedSeq_ is only ever accessed from onFrames() so no
//   synchronization is needed there.
//   state_ is std::atomic<State> so state() may be called safely from any
//   thread (consistent with droppedSamples()).

#include <QMutex>
#include <QObject>
#include <QThread>
#include <QElapsedTimer>
#include <QVector>
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>

#include "app/AppState.h"
#include "core/acquisition/IDataSource.h"
#include "core/acquisition/SerialSource.h"
#include "core/device/DeviceConfig.h"
#include "core/dsp/RingBuffer.h"
#include "core/recording/Recorder.h"
#include "core/recording/SessionMetadata.h"

namespace studio {

class SessionController : public QObject
{
    Q_OBJECT

public:
    // Takes ownership of source. Does NOT start the worker thread; call
    // startStreaming() for that. This keeps construction cheap and test-safe.
    explicit SessionController(IDataSource* source,
                               size_t displayBufferCapacity = (1 << 16),
                               QObject* parent = nullptr);

    ~SessionController() override;

    // ---- State / metrics --------------------------------------------------
    State    state()          const;      // may be called from any thread
    uint64_t droppedSamples() const;      // may be called from any thread
    RingBuffer<EegFrame>& displayBuffer();

    // ---- Frame handler (public test seam) ---------------------------------
    // Normally connected to source->framesReady via a queued connection so
    // it executes on the controller thread. Exposed publicly so unit tests
    // can drive seq-gap logic directly without starting the worker thread.
    void onFrames(const EegFrameBatch& batch);

    // ---- Device config -----------------------------------------------------
    DeviceConfig config() const;
    void applyConfig(const DeviceConfig& cfg);

    // ---- Per-channel rolling history (non-destructive, for SpectrumView) ---
    // Returns up to @p maxCount most-recent samples for @p channel (0-based),
    // in chronological order (oldest→newest). Thread-safe.
    // Returns empty QVector for out-of-range channel.
    QVector<double> recentSamples(int channel, int maxCount) const;

    // ---- Recording lifecycle ----------------------------------------------
    // Returns false if not currently Streaming or if the recorder fails to open.
    // On success sets state to Recording, starts the record clock, and emits
    // stateChanged + recordingChanged(true). Recorder I/O runs on writerThread_.
    bool     startRecording(const QString& basePath, const SessionMetadata& meta);
    void     stopRecording();
    void     addMarker(const QString& label);
    quint64  recordedSamples() const;  // 0 unless currently Recording
    int      markerCount()     const;  // event markers added in the current session

    // ---- Source swap -------------------------------------------------------
    // Swap to a new IDataSource at runtime (e.g. switching from Simulated to
    // Serial). If streaming, stopStreaming() is called first. The OLD source is
    // safely destroyed on its own thread; the NEW source is moved to the
    // (restarted) workerThread_.  Does NOT auto-start; call startStreaming().
    void setSource(IDataSource* newSource);

    // Convenience: create a SerialSource, open portName, and if successful call
    // setSource(). Returns true on success.  On failure, deletes the source and
    // returns false. Logs the outcome.
    bool connectSerial(const QString& portName, int baud = 921600);

    // Convenience: detect and open the first MMB0 USB device (VID=0x0451,
    // PID=0x5718). Creates an Mmb0UsbTransport + Mmb0DataSource and calls
    // setSource(). Returns true on success. If no device is present or open
    // fails, returns false without touching the current source.
    bool connectMmb0();

public slots:
    void startStreaming();
    void stopStreaming();

signals:
    void metricsUpdated(studio::Metrics metrics);
    void stateChanged(studio::State state);
    void errorOccurred(QString message);
    void recordingChanged(bool recording);
    void configChanged(studio::DeviceConfig config);

private slots:
    void onSourceError(const QString& message);

private:
    void setState(State s);

    // Owned objects
    IDataSource*          source_;        // lives on workerThread_
    QThread               workerThread_;
    RingBuffer<EegFrame>  displayBuffer_;

    // Recording — recorder_ lives on its own writerThread_ so all BDF/CSV disk
    // I/O happens off the controller/GUI thread.
    Recorder*             recorder_      = nullptr;  // lives on writerThread_
    QThread               writerThread_;
    QElapsedTimer         recordClock_;              // started on startRecording
    int                   markerCount_   = 0;

    // Mutable state — only written from onFrames() (controller thread)
    std::atomic<uint64_t> droppedSamples_{0};
    uint64_t              expectedSeq_   = 0;
    bool                  firstFrame_    = true;

    // Metrics helpers
    QElapsedTimer         elapsedTimer_;
    uint64_t              samplesSeen_   = 0;

    // Device configuration
    DeviceConfig          config_;

    // Session state — atomic so state() is safe to call from any thread.
    std::atomic<State>    state_         {State::Idle};

    // Per-channel rolling history (non-destructive snapshot for SpectrumView).
    // Capped at kRecentCapacity samples per channel.
    static constexpr int kRecentCapacity = 16384;
    mutable QMutex                        recentMutex_;
    std::array<std::deque<double>, 8>     recent_;
};

} // namespace studio
