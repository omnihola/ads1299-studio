#include <QtTest>
#include <QSignalSpy>
#include "core/acquisition/SimulatedSource.h"

using namespace studio;

class TestSim : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qRegisterMetaType<EegFrameBatch>();
    }

    void deterministic_with_seed() {
        SimulatedSource a(7), b(7);
        EegFrameBatch ba = a.generateBatch(64);
        EegFrameBatch bb = b.generateBatch(64);
        QCOMPARE(ba.size(), size_t(64));
        QCOMPARE(ba[10].ch[0], bb[10].ch[0]);
    }

    void seq_is_monotonic() {
        SimulatedSource a(1);
        auto b1 = a.generateBatch(10);
        auto b2 = a.generateBatch(10);
        QCOMPARE(b1.front().seq, 0u);
        QCOMPARE(b2.front().seq, 10u);
    }
};

QTEST_MAIN(TestSim)
#include "test_simulatedsource.moc"
