#include <QtTest>
class TestSmoke : public QObject { Q_OBJECT
private slots:
    void sanity() { QCOMPARE(1 + 1, 2); }
};
QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"
