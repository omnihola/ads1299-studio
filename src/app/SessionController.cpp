// src/app/SessionController.cpp
#include "app/SessionController.h"
#include "core/acquisition/SerialSource.h"
#include "core/acquisition/mmb0/Mmb0Bootloader.h"
#include "core/acquisition/mmb0/Mmb0DataSource.h"
#include "core/acquisition/mmb0/Mmb0UsbTransport.h"
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

    // Free source_ now that workerThread_ is fully stopped (quit()+wait()
    // completed above). A QObject whose owning thread has been joined and has
    // no pending events is safe to delete directly from any thread — Qt only
    // warns on moveToThread / cross-thread event delivery, not on plain delete.
    // We deliberately skip moveToThread to avoid triggering that very warning.
    if (source_) {
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
    // Guard: config is frozen while a recording is in progress. A mid-recording
    // change would silently desync the BDF time-base (fixed at Recorder::open())
    // and the µV scaling (also fixed at open()), so we reject it entirely.
    if (state_.load(std::memory_order_relaxed) == State::Recording) {
        Logger::instance().log("warn", "SessionController.applyConfig",
                               QJsonObject{{"ignored", "recording in progress"}});
        return;
    }

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

    // Reset per-session integrity counters so each streaming session starts
    // fresh.  This prevents stale drop counts and seq-gap misdetection on
    // stop/restart cycles (FIX 1).
    droppedSamples_.store(0, std::memory_order_relaxed);
    expectedSeq_ = 0;
    firstFrame_  = true;

    setState(State::Streaming);
    Logger::instance().log("info", "SessionController.startStreaming", {});

    workerThread_.start();

    // Sync the source's sample rate to the configured rate before starting.
    // This ensures the source matches config_ at stream startup, preventing the
    // divergence where applyConfig hasn't been called by the user yet.
    QMetaObject::invokeMethod(source_,
        [this, sps = config_.sampleRate()]() {
            source_->setSampleRate(sps);
        },
        Qt::QueuedConnection);

    // Start the source on the worker thread via a queued invocation.
    QMetaObject::invokeMethod(source_, &IDataSource::start, Qt::QueuedConnection);
}

void SessionController::stopStreaming()
{
    if (state_ == State::Idle) return;

    // Finalize an in-progress recording FIRST (closes the BDF and emits
    // recordingChanged(false)). Otherwise the recorder would be left open —
    // the UI stuck in "recording" while nothing is written — until app exit.
    if (state_ == State::Recording) {
        stopRecording();
    }

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

void SessionController::setSource(IDataSource* newSource, SourceType type)
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
    //    workerThread_ is NOT running at this point (stopStreaming() above
    //    called quit()+wait(), so the thread is fully joined and has no pending
    //    events). A direct delete is safe and avoids a moveToThread call on an
    //    object whose affinity points to a dead thread — which would otherwise
    //    print a Qt "Cannot move to target thread" warning.
    delete oldSource;

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
    sourceType_ = type;

    const QString name = newSource->capabilities().name;
    Logger::instance().log("info", "SessionController.setSource",
                           QJsonObject{{"name", name}});
    emit sourceChanged(type, name);
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
    setSource(serial, SourceType::Serial);
    Logger::instance().log("info", "SessionController.connectSerial",
                           QJsonObject{{"port", portName}, {"baud", baud}});
    return true;
}

// Ensure the board is in Styx mode (0451:5718). The board reverts to the ROM
// bootloader (0451:9001) on every power cycle — when only the bootloader is
// present, locate/validate the firmware image and upload it.
// Returns false with a user-facing message in *errorOut on failure.
bool SessionController::ensureMmb0StyxMode(QString* errorOut)
{
    using studio::mmb0::Mmb0Bootloader;

    auto fail = [&](const QString& msg, const char* event) {
        Logger::instance().log("warn", event, QJsonObject{{"error", msg}});
        if (errorOut) *errorOut = msg;
        return false;
    };

    if (Mmb0Bootloader::isStyxPresent())
        return true;

    if (!Mmb0Bootloader::isBootloaderPresent()) {
        Logger::instance().log("info", "SessionController.connectMmb0.noDevice",
                               QJsonObject{{"vid", "0x0451"},
                                           {"pid", "0x5718/0x9001"}});
        if (errorOut)
            *errorOut = QStringLiteral(
                "No MMB0 device found (neither 0451:5718 nor 0451:9001)");
        return false;
    }

    const QString fw =
        Mmb0Bootloader::locateFirmware(Mmb0Bootloader::defaultCandidates());
    QString detail;
    const auto status = Mmb0Bootloader::validateFirmware(fw, &detail);
    switch (status) {
    case Mmb0Bootloader::FirmwareStatus::Ok:
        break;
    case Mmb0Bootloader::FirmwareStatus::UnknownImage:
        // Not the known release image — a wrong image just fails to boot
        // the DSP (board stays in 9001), so proceed with a warning.
        Logger::instance().log("warn",
                               "SessionController.connectMmb0.unknownFirmware",
                               QJsonObject{{"detail", detail}, {"path", fw}});
        break;
    default:
        return fail(QStringLiteral(
                        "MMB0 is in bootloader mode but no firmware image is "
                        "available (%1). Set the \"mmb0/firmwarePath\" setting "
                        "or ADS1299_FIRMWARE to your ads1299evm-pdk.bin.")
                        .arg(detail),
                    "SessionController.connectMmb0.firmwareMissing");
    }

    Mmb0Bootloader boot;
    Logger::instance().log("info", "SessionController.connectMmb0.uploadFirmware",
                           QJsonObject{{"path", fw}});
    if (!boot.uploadFirmware(fw))
        return fail(QStringLiteral("Firmware upload failed: %1").arg(boot.lastError()),
                    "SessionController.connectMmb0.uploadFailed");
    if (!boot.waitForStyx())
        return fail(QStringLiteral("Firmware uploaded but %1").arg(boot.lastError()),
                    "SessionController.connectMmb0.reenumTimeout");
    return true;
}

bool SessionController::connectMmb0(QString* errorOut)
{
    if (!ensureMmb0StyxMode(errorOut))
        return false;

    auto* t = new studio::mmb0::Mmb0UsbTransport();
    if (!t->open()) {
        const QString msg = QStringLiteral("MMB0 open failed: %1").arg(t->lastError());
        delete t;
        Logger::instance().log("warn", "SessionController.connectMmb0.openFailed",
                               QJsonObject{{"error", msg}});
        if (errorOut) *errorOut = msg;
        return false;
    }

    auto* src = new studio::mmb0::Mmb0DataSource(t); // src takes ownership of t
    src->setConfig(config_);
    setSource(src, SourceType::Mmb0);
    Logger::instance().log("info", "SessionController.connectMmb0.connected", {});
    return true;
}

// ---------------------------------------------------------------------------
// Recording lifecycle
// ---------------------------------------------------------------------------

bool SessionController::startRecording(const QString& basePath,
                                       const SessionMetadata& meta,
                                       bool writeCsv)
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
        [this, &basePath, &effectiveMeta, &opened, writeCsv]() {
            opened = recorder_->open(basePath, effectiveMeta, writeCsv);
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

int SessionController::markerCount() const
{
    // Reset on startRecording and only mutated from the GUI thread (addMarker),
    // same thread as the readout that reads it — no synchronization needed.
    return markerCount_;
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
