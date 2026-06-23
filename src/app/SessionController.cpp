// src/app/SessionController.cpp
#include "app/SessionController.h"
#include "core/logging/Logger.h"

#include <QJsonObject>

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
