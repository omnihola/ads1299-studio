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

#include <QObject>
#include <QThread>
#include <QElapsedTimer>
#include <atomic>
#include <cstdint>
#include <memory>

#include "app/AppState.h"
#include "core/acquisition/IDataSource.h"
#include "core/dsp/RingBuffer.h"

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

public slots:
    void startStreaming();
    void stopStreaming();

signals:
    void metricsUpdated(studio::Metrics metrics);
    void stateChanged(studio::State state);
    void errorOccurred(QString message);

private slots:
    void onSourceError(const QString& message);

private:
    void setState(State s);

    // Owned objects
    IDataSource*          source_;        // lives on workerThread_
    QThread               workerThread_;
    RingBuffer<EegFrame>  displayBuffer_;

    // Mutable state — only written from onFrames() (controller thread)
    std::atomic<uint64_t> droppedSamples_{0};
    uint64_t              expectedSeq_   = 0;
    bool                  firstFrame_    = true;

    // Metrics helpers
    QElapsedTimer         elapsedTimer_;
    uint64_t              samplesSeen_   = 0;

    // Session state — atomic so state() is safe to call from any thread.
    std::atomic<State>    state_         {State::Idle};
};

} // namespace studio
