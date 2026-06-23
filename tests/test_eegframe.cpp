#include <QtTest>
#include "core/acquisition/EegFrame.h"

using studio::int24ToInt32;

class TestEegFrame : public QObject {
    Q_OBJECT
private slots:
    void int24_sign_extends_negative() {
        // 0xFFFFFF == -1 as 24-bit two's complement
        QCOMPARE(int24ToInt32(0xFF, 0xFF, 0xFF), -1);
    }
    void int24_positive() {
        QCOMPARE(int24ToInt32(0x00, 0x00, 0x01), 1);
        QCOMPARE(int24ToInt32(0x7F, 0xFF, 0xFF), 8388607);
    }
};

QTEST_MAIN(TestEegFrame)
#include "test_eegframe.moc"
