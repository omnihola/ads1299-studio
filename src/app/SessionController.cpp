// src/app/SessionController.cpp
#include "app/SessionController.h"
#include "core/logging/Logger.h"

#include <QJsonObject>
#include <QMetaType>

namespace studio {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

SessionController::SessionController(IDataSource* source,
                                     size_t displayBufferCapacity,
                                     QObject* parent)
    : QObject(parent)
    , source_(source)
    , displayBuffer_(displayBufferCapacity)
{
    // Move the source onto the worker thread.
    // The thread is created but NOT started here — startStreaming() starts it.
    source_->setParent(nullptr); // required before moveToThread
    source_->moveToThread(&workerThread_);

    // Queued connection: framesReady fires on workerThread_; onFrames() runs
    // on the thread that owns *this* (typically the GUI/main thread).
    connect(source_, &IDataSource::framesReady,
            this,    &SessionController::onFrames,
            Qt::QueuedConnection);

    connect(source_, &IDataSource::errorOccurred,
            this,    &SessionController::onSourceError,
            Qt::QueuedConnection);

    elapsedTimer_.start();

    // Register EegFrameBatch so it can cross thread boundaries via a queued
    // connection (writeBatch is invoked on writerThread_). Idempotent.
    qRegisterMetaType<studio::EegFrameBatch>("studio::EegFrameBatch");

    // Create the recorder and move it to the dedicated writer thread. The
    // writer thread runs for the controller's lifetime so invokeMethod can
    // dispatch open/writeBatch/addAnnotation/close to it.
    recorder_ = new Recorder(nullptr);
    recorder_->moveToThread(&writerThread_);
    connect(&writerThread_, &QThread::finished, recorder_, &QObject::deleteLater);
    writerThread_.start();
}

SessionController::~SessionController()
{
    // Ensure the source is stopped and the thread is cleaned up.
    // source_ lives on workerThread_, so we must invoke stop() via a queued
    // call rather than calling it directly (which would be a data race).
    if (workerThread_.isRunning()) {
        QMetaObject::invokeMethod(source_, &IDataSource::stop, Qt::BlockingQueuedConnection);
        workerThread_.quit();
        workerThread_.wait();
    }

    // Stop the writer thread; finalise any in-progress recording first.
    if (writerThread_.isRunning()) {
        if (recorder_ && recorder_->isOpen()) {
            QMetaObject::invokeMethod(recorder_, &Recorder::close,
                                      Qt::BlockingQueuedConnection);
        }
        writerThread_.quit();
        writerThread_.wait();
    }
}

// ---------------------------------------------------------------------------
// Public accessors
// ---------------------------------------------------------------------------

State SessionController::state() const
{
    return state_.load(std::memory_order_relaxed);
}

uint64_t SessionController::droppedSamples() const
{
    return droppedSamples_.load(std::memory_order_relaxed);
}

RingBuffer<EegFrame>& SessionController::displayBuffer()
{
    return displayBuffer_;
}

// ---------------------------------------------------------------------------
// Public slots
// ---------------------------------------------------------------------------

void SessionController::startStreaming()
{
    if (state_ != State::Idle) return;

    setState(State::Streaming);
    Logger::instance().log("info", "SessionController.startStreaming", {});

    workerThread_.start();

    // Start the source on the worker thread via a queued invocation.
    QMetaObject::invokeMethod(source_, &IDataSource::start, Qt::QueuedConnection);
}

void SessionController::stopStreaming()
{
    if (state_ == State::Idle) return;

    // Stop the source on the worker thread and then wait for it to finish.
    QMetaObject::invokeMethod(source_, &IDataSource::stop, Qt::BlockingQueuedConnection);
    workerThread_.quit();
    workerThread_.wait();

    setState(State::Idle);
    Logger::instance().log("info", "SessionController.stopStreaming", {});
}

// ---------------------------------------------------------------------------
// Recording lifecycle
// ---------------------------------------------------------------------------

bool SessionController::startRecording(const QString& basePath,
                                       const SessionMetadata& meta)
{
    if (state_.load(std::memory_order_relaxed) != State::Streaming) {
        Logger::instance().log("warn", "SessionController.startRecording.notStreaming", {});
        return false;
    }

    // Open the recorder ON the writer thread and capture the result.
    bool opened = false;
    QMetaObject::invokeMethod(recorder_,
        [this, &basePath, &meta, &opened]() {
            opened = recorder_->open(basePath, meta);
        },
        Qt::BlockingQueuedConnection);

    if (!opened) {
        Logger::instance().log("error", "SessionController.startRecording.openFailed",
                               QJsonObject{{"basePath", basePath}});
        return false;
    }

    recordClock_.restart();
    markerCount_ = 0;
    setState(State::Recording);
    emit recordingChanged(true);
    Logger::instance().log("info", "SessionController.startRecording",
                           QJsonObject{{"basePath", basePath}});
    return true;
}

void SessionController::stopRecording()
{
    if (state_.load(std::memory_order_relaxed) != State::Recording) return;

    QMetaObject::invokeMethod(recorder_, &Recorder::close,
                              Qt::BlockingQueuedConnection);
    setState(State::Streaming);
    emit recordingChanged(false);
    Logger::instance().log("info", "SessionController.stopRecording",
                           QJsonObject{{"markerCount", markerCount_}});
}

void SessionController::addMarker(const QString& label)
{
    if (state_.load(std::memory_order_relaxed) != State::Recording) return;

    const double onsetSec = recordClock_.elapsed() / 1000.0;
    QMetaObject::invokeMethod(recorder_,
        [this, onsetSec, label]() {
            recorder_->addAnnotation(onsetSec, label);
        },
        Qt::QueuedConnection);
    ++markerCount_;
    Logger::instance().log("info", "SessionController.addMarker",
                           QJsonObject{{"label", label}, {"onsetSec", onsetSec}});
}

quint64 SessionController::recordedSamples() const
{
    if (state_.load(std::memory_order_relaxed) != State::Recording) return 0;
    // recorder_ lives on writerThread_; samplesWritten() is a plain counter
    // read. The race is benign for a display-only readout.
    return recorder_->samplesWritten();
}

// ---------------------------------------------------------------------------
// Frame handler (public test seam + queued-connection target)
// ---------------------------------------------------------------------------

void SessionController::onFrames(const EegFrameBatch& batch)
{
    for (const EegFrame& frame : batch) {
        // --- Seq-continuity check ---
        if (firstFrame_) {
            // First frame seen: initialise the counter — no drop counted.
            expectedSeq_ = frame.seq;
            firstFrame_  = false;
            // Advance past this first frame.
            expectedSeq_ = static_cast<uint64_t>(frame.seq) + 1;
        } else if (frame.seq >= expectedSeq_) {
            if (frame.seq > expectedSeq_) {
                // Gap detected: (frame.seq - expectedSeq_) samples are missing.
                uint64_t gap = static_cast<uint64_t>(frame.seq - expectedSeq_);
                droppedSamples_.fetch_add(gap, std::memory_order_relaxed);

                Logger::instance().log("warn", "SessionController.seqGap",
                    QJsonObject{
                        {"expected", static_cast<qint64>(expectedSeq_)},
                        {"received", static_cast<qint64>(frame.seq)},
                        {"gap",      static_cast<qint64>(gap)}
                    });
            }
            // Advance the counter past the frame we just processed.
            expectedSeq_ = static_cast<uint64_t>(frame.seq) + 1;
        } else {
            // Retrograde / duplicate frame: seq < expectedSeq_.
            // Do NOT advance expectedSeq_ — that would make the next normal
            // frame look like a huge spurious gap.
            Logger::instance().log("warn", "retrograde_seq",
                QJsonObject{
                    {"seq",      static_cast<qint64>(frame.seq)},
                    {"expected", static_cast<qint64>(expectedSeq_)}
                });
        }

        // --- Push to display ring buffer ---
        displayBuffer_.push(frame);

        // --- SPS accounting ---
        ++samplesSeen_;
    }

    // --- Emit metrics (running estimate) ---
    qint64 elapsedMs = elapsedTimer_.elapsed();
    double sps = (elapsedMs > 0)
               ? (static_cast<double>(samplesSeen_) * 1000.0 / static_cast<double>(elapsedMs))
               : 0.0;

    Metrics m;
    m.sps            = sps;
    m.droppedSamples = droppedSamples_.load(std::memory_order_relaxed);
    m.bufferFill     = displayBuffer_.size();

    // Forward the whole batch to the recorder (on the writer thread) when
    // recording. Non-blocking — passes the batch by value via the metatype.
    if (state_.load(std::memory_order_relaxed) == State::Recording) {
        QMetaObject::invokeMethod(recorder_, "writeBatch", Qt::QueuedConnection,
                                  Q_ARG(studio::EegFrameBatch, batch));
    }

    emit metricsUpdated(m);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void SessionController::onSourceError(const QString& message)
{
    Logger::instance().log("error", "SessionController.sourceError",
                           QJsonObject{{"message", message}});
    emit errorOccurred(message);
}

void SessionController::setState(State s)
{
    if (state_.load(std::memory_order_relaxed) == s) return;
    state_.store(s, std::memory_order_relaxed);
    emit stateChanged(s);
}

} // namespace studio
