#include <QtTest>
#include "core/dsp/RingBuffer.h"
using studio::RingBuffer;
class TestRingBuffer : public QObject { Q_OBJECT
private slots:
    void push_pop_fifo() {
        RingBuffer<int> rb(4);
        QVERIFY(rb.push(1)); QVERIFY(rb.push(2));
        int v=0; QVERIFY(rb.pop(v)); QCOMPARE(v,1);
        QVERIFY(rb.pop(v)); QCOMPARE(v,2);
        QVERIFY(!rb.pop(v)); // empty
    }
    void overflow_counts_drops() {
        RingBuffer<int> rb(2);
        QVERIFY(rb.push(1)); QVERIFY(rb.push(2));
        QVERIFY(!rb.push(3));          // full -> reject
        QCOMPARE(rb.dropped(), 1ull);
    }
};
QTEST_MAIN(TestRingBuffer)
#include "test_ringbuffer.moc"
