// tests/test_sessioncontroller.cpp
// QtTest — drives SessionController.onFrames() directly (no thread started).

#include <QtTest>
#include "app/SessionController.h"
#include "core/acquisition/SimulatedSource.h"

namespace {

// Helper: build an EegFrameBatch from a list of seq values.
studio::EegFrameBatch makeBatch(std::initializer_list<uint32_t> seqs)
{
    studio::EegFrameBatch batch;
    batch.reserve(seqs.size());
    for (uint32_t s : seqs) {
        studio::EegFrame f;
        f.seq = s;
        batch.push_back(f);
    }
    return batch;
}

} // anonymous namespace

class TestSessionController : public QObject
{
    Q_OBJECT

private slots:
    // -----------------------------------------------------------------------
    // Test 1: gap detection — one missing sample (seq 11 skipped)
    // -----------------------------------------------------------------------
    void test_seqGapDetected()
    {
        // Construct controller; SimulatedSource ownership is transferred.
        studio::SessionController ctrl(new studio::SimulatedSource(1));

        // Batch A: first frames ever seen — seq 8, 9, 10 (contiguous from cold start)
        auto batchA = makeBatch({8, 9, 10});
        ctrl.onFrames(batchA);
        QCOMPARE(ctrl.droppedSamples(), uint64_t(0));

        // Batch B: seq 12, 13 — seq 11 is missing → 1 dropped sample
        // After batchA: expectedSeq_ = 11.
        // frame seq=12: gap = 12-11 = 1 dropped. expectedSeq_ → 13.
        // frame seq=13: gap = 0. expectedSeq_ → 14.
        // Total drops = 1.
        auto batchB = makeBatch({12, 13});
        ctrl.onFrames(batchB);
        QCOMPARE(ctrl.droppedSamples(), uint64_t(1));
    }

    // -----------------------------------------------------------------------
    // Test 2: contiguous stream across two batches → zero drops
    // -----------------------------------------------------------------------
    void test_contiguousNoDrop()
    {
        studio::SessionController ctrl(new studio::SimulatedSource(2));

        auto batchA = makeBatch({0, 1, 2, 3, 4});
        ctrl.onFrames(batchA);
        QCOMPARE(ctrl.droppedSamples(), uint64_t(0));

        auto batchB = makeBatch({5, 6, 7, 8, 9});
        ctrl.onFrames(batchB);
        QCOMPARE(ctrl.droppedSamples(), uint64_t(0));
    }

    // -----------------------------------------------------------------------
    // Test 3: multiple gaps accumulate correctly
    // After seq 0-4 (no gap), send seq 6 then 8:
    //   expectedSeq_=5 → frame 6: gap=1; expectedSeq_=7 → frame 8: gap=1 → total=2
    // -----------------------------------------------------------------------
    void test_multipleGapsAccumulate()
    {
        studio::SessionController ctrl(new studio::SimulatedSource(3));

        ctrl.onFrames(makeBatch({0, 1, 2, 3, 4}));
        QCOMPARE(ctrl.droppedSamples(), uint64_t(0));

        ctrl.onFrames(makeBatch({6, 8}));
        QCOMPARE(ctrl.droppedSamples(), uint64_t(2));
    }

    // -----------------------------------------------------------------------
    // Test 4: frames land in the display ring buffer
    // -----------------------------------------------------------------------
    void test_framesInRingBuffer()
    {
        studio::SessionController ctrl(new studio::SimulatedSource(4));

        ctrl.onFrames(makeBatch({0, 1, 2}));
        QCOMPARE(ctrl.displayBuffer().size(), size_t(3));
    }

    // -----------------------------------------------------------------------
    // Test 5: state starts Idle
    // -----------------------------------------------------------------------
    void test_initialStateIdle()
    {
        studio::SessionController ctrl(new studio::SimulatedSource(5));
        QCOMPARE(ctrl.state(), studio::State::Idle);
    }

    // -----------------------------------------------------------------------
    // Test 6: retrograde frame does not count as a drop and does not corrupt
    //         expectedSeq_ so that the next in-order frame looks like a gap.
    //
    // Sequence:
    //   Batch A: 10, 11, 12  → expectedSeq_ = 13, droppedSamples = 0
    //   Batch B: 11 (retro), 13 (exact match)
    //            - seq 11 < 13 → retrograde, NOT a drop, expectedSeq_ stays 13
    //            - seq 13 == 13 → no gap, expectedSeq_ → 14
    //   Final: droppedSamples == 0
    // -----------------------------------------------------------------------
    void test_retrogradeSeqNoDrop()
    {
        studio::SessionController ctrl(new studio::SimulatedSource(6));

        // Establish baseline: seq 10, 11, 12 → expectedSeq_ = 13
        ctrl.onFrames(makeBatch({10, 11, 12}));
        QCOMPARE(ctrl.droppedSamples(), uint64_t(0));

        // Retrograde seq 11 followed by forward seq 13 (exactly expected).
        ctrl.onFrames(makeBatch({11, 13}));

        // Retrograde must not be counted as a drop, and seq 13 == expectedSeq_
        // so no gap is detected either.
        QCOMPARE(ctrl.droppedSamples(), uint64_t(0));
    }

    // -----------------------------------------------------------------------
    // Test 7: live worker-thread streaming path delivers frames
    // Exercises the REAL moveToThread + QTimer path that had the bug.
    // Before the fix: timer_ stayed on the GUI thread, timer never fired,
    //                 displayBuffer() stayed empty.
    // After the fix:  timer_ is a child → migrates with moveToThread → fires
    //                 on the worker thread → frames arrive in displayBuffer().
    // -----------------------------------------------------------------------
    void test_liveStreamingDeliversFrames()
    {
        studio::SessionController ctrl(new studio::SimulatedSource(7));

        ctrl.startStreaming();
        QTest::qWait(300);  // let worker thread + timer produce frames

        QVERIFY2(ctrl.displayBuffer().size() > 0,
                 "displayBuffer must be non-empty after 300ms of live streaming");

        ctrl.stopStreaming();
    }

    // -----------------------------------------------------------------------
    // Test 8: per-session integrity counters reset across stop/start
    //
    // FIX 1 verification: droppedSamples_ must be 0 after a stop→start cycle
    // even if a gap was introduced in the previous session.  Before the fix
    // the counter was cumulative and the old value persisted into the next run.
    // -----------------------------------------------------------------------
    void test_dropCountResetsAcrossSessions()
    {
        studio::SessionController ctrl(new studio::SimulatedSource(8));

        // Session 1: inject a seq gap so droppedSamples becomes > 0.
        // Batch A: seq 0,1,2  → firstFrame_ initialised, expectedSeq_=3, drops=0
        // Batch B: seq 5,6    → gap of 2 (seq 3 and 4 missing) → drops=2
        ctrl.onFrames(makeBatch({0, 1, 2}));
        QCOMPARE(ctrl.droppedSamples(), uint64_t(0));

        ctrl.onFrames(makeBatch({5, 6}));
        QVERIFY2(ctrl.droppedSamples() > 0,
                 "Expected droppedSamples > 0 after a seq gap in session 1");

        // Simulate stop/start: the controller must reset its counters at
        // startStreaming().  We call startStreaming() but don't want to block
        // waiting for the worker thread — stopStreaming() first (Idle guard),
        // then re-check the counter immediately after startStreaming() returns
        // (the reset happens before the thread is launched).
        //
        // Note: ctrl.state() is Idle (we never called startStreaming before),
        // so startStreaming() will transition Idle→Streaming and reset counters.
        ctrl.startStreaming();

        // The counter reset happens synchronously at the top of startStreaming()
        // before any worker thread activity.
        QCOMPARE(ctrl.droppedSamples(), uint64_t(0));

        ctrl.stopStreaming();
    }
};

QTEST_MAIN(TestSessionController)
#include "test_sessioncontroller.moc"
