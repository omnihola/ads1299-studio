// src/app/SessionController.cpp
#include "app/SessionController.h"
#include "core/acquisition/SerialSource.h"
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

    // Register metatypes so these types can cross thread boundaries via queued
    // connections. Idempotent.
    qRegisterMetaType<studio::EegFrameBatch>("studio::EegFrameBatch");
    qRegisterMetaType<studio::DeviceConfig>("studio::DeviceConfig");

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

    // FIX 2: Free source_ now that workerThread_ is stopped. Move it back to
    // the current thread (required for a direct delete) then delete directly —
    // NOT deleteLater, as the event loop may not be running during shutdown.
    // source_ has no QObject parent (ctor calls setParent(nullptr)) so there
    // is no risk of a double-delete.
    if (source_) {
        source_->moveToThread(QThread::currentThread());
        delete source_;
        source_ = nullptr;
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

QVector<double> SessionController::recentSamples(int channel, int maxCount) const
{
    if (channel < 0 || channel >= 8) {
        return {};
    }
    QMutexLocker lock(&recentMutex_);
    const auto& dq = recent_[static_cast<size_t>(channel)];
    const int available = static_cast<int>(dq.size());
    const int count = std::min(available, maxCount);
    QVector<double> result;
    result.reserve(count);
    // Return the most-recent `count` samples in chronological (oldest→newest) order.
    const int start = available - count;
    for (int i = start; i < available; ++i) {
        result.push_back(dq[static_cast<size_t>(i)]);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Device config
// ---------------------------------------------------------------------------

DeviceConfig SessionController::config() const
{
    return config_;
}

void SessionController::applyConfig(const DeviceConfig& cfg)
{
    config_ = cfg;
    Logger::instance().log("info", "SessionController.applyConfig",
                           QJsonObject{{"sampleRate", cfg.sampleRate()}});

    // Forward sample rate to the source on the worker thread (non-blocking).
    QMetaObject::invokeMethod(source_,
        [this, sps = cfg.sampleRate()]() {
            source_->setSampleRate(sps);
        },
        Qt::QueuedConnection);

    emit configChanged(cfg);
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
// Source swap
// ---------------------------------------------------------------------------

void SessionController::setSource(IDataSource* newSource)
{
    Q_ASSERT(newSource);

    // 1. Stop streaming if currently active.
    if (state_ != State::Idle) {
        stopStreaming();
    }

    // 2. Disconnect the old source's signals from this controller.
    IDataSource* oldSource = source_;
    disconnect(oldSource, &IDataSource::framesReady,
               this,      &SessionController::onFrames);
    disconnect(oldSource, &IDataSource::errorOccurred,
               this,      &SessionController::onSourceError);

    // 3. Destroy the old source safely.
    //    workerThread_ is NOT running at this point (stopStreaming() quit it).
    //    Move the old source back to this object's thread so deleteLater()
    //    runs on the controller thread's event loop (safe, no race).
    oldSource->moveToThread(this->thread());
    oldSource->deleteLater();

    // 4. Wire the new source onto the worker thread.
    newSource->setParent(nullptr);
    newSource->moveToThread(&workerThread_);

    connect(newSource, &IDataSource::framesReady,
            this,      &SessionController::onFrames,
            Qt::QueuedConnection);
    connect(newSource, &IDataSource::errorOccurred,
            this,      &SessionController::onSourceError,
            Qt::QueuedConnection);

    source_ = newSource;

    Logger::instance().log("info", "SessionController.setSource",
                           QJsonObject{{"name", newSource->capabilities().name}});
}

bool SessionController::connectSerial(const QString& portName, int baud)
{
    auto* serial = new SerialSource();
    if (!serial->openPort(portName, baud)) {
        delete serial;
        Logger::instance().log("warn", "SessionController.connectSerial.failed",
                               QJsonObject{{"port", portName}});
        return false;
    }
    setSource(serial);
    Logger::instance().log("info", "SessionController.connectSerial",
                           QJsonObject{{"port", portName}, {"baud", baud}});
    return true;
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

    // Snapshot the current device config into the metadata so the recording
    // captures the real hardware configuration (not the caller's default).
    const SessionMetadata effectiveMeta = meta
        .withDeviceConfigSnapshot(config_.toJson())
        .withSampleRate(config_.sampleRate());

    // Open the recorder ON the writer thread and capture the result.
    bool opened = false;
    QMetaObject::invokeMethod(recorder_,
        [this, &basePath, &effectiveMeta, &opened]() {
            opened = recorder_->open(basePath, effectiveMeta);
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
                           QJsonObject{{"basePath", basePath},
                                       {"sampleRate", config_.sampleRate()}});
    return true;
}

void SessionController::stopRecording()
{
    if (state_.load(std::memory_order_relaxed) != State::Recording) return;

    // Flip state BEFORE closing the recorder. onFrames only forwards to the
    // recorder while state()==Recording, so changing state first stops any new
    // writeBatch events from being enqueued. Batches already posted to the
    // writer thread still drain first (FIFO event queue) because the close
    // event is posted after them.
    setState(State::Streaming);
    emit recordingChanged(false);
    QMetaObject::invokeMethod(recorder_, &Recorder::close,
                              Qt::BlockingQueuedConnection);
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
    const bool isRecording = (state_.load(std::memory_order_relaxed) == State::Recording);

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

                // FIX 1: While recording, pad the BDF/CSV timeline with zeros
                // to preserve alignment with true acquisition time, and add a
                // BDF+ annotation marking the drop. Both are dispatched to the
                // writer thread (QueuedConnection) BEFORE the frame's writeBatch
                // so ordering is correct (FIFO event queue).
                if (isRecording) {
                    const double onsetSec = recordClock_.elapsed() / 1000.0;
                    QMetaObject::invokeMethod(recorder_, "writeGap",
                                              Qt::QueuedConnection,
                                              Q_ARG(quint32, static_cast<quint32>(gap)));
                    QMetaObject::invokeMethod(recorder_,
                        [this, onsetSec, gap]() {
                            recorder_->addAnnotation(
                                onsetSec,
                                QString("drop: %1 samples").arg(gap));
                        },
                        Qt::QueuedConnection);
                }
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

        // --- Update rolling history for SpectrumView (non-destructive) ---
        {
            QMutexLocker lock(&recentMutex_);
            for (int c = 0; c < 8; ++c) {
                recent_[static_cast<size_t>(c)].push_back(
                    static_cast<double>(frame.ch[c]));
                if (static_cast<int>(recent_[static_cast<size_t>(c)].size())
                        > kRecentCapacity) {
                    recent_[static_cast<size_t>(c)].pop_front();
                }
            }
        }

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
    if (!batch.empty()) {
        m.leadOffP = batch.back().statP;
        m.leadOffN = batch.back().statN;
    }

    // Forward the whole batch to the recorder (on the writer thread) when
    // recording. Non-blocking — passes the batch by value via the metatype.
    // Any gap writeGap/addAnnotation events posted above are already queued
    // ahead of this writeBatch call (FIFO), so ordering is guaranteed.
    if (isRecording) {
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
