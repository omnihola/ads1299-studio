#include <QtTest/QtTest>
#include "ui/theme/LedIndicator.h"

class TestLedIndicator : public QObject {
    Q_OBJECT

private slots:
    void colorFor_Off() {
        studio::LedIndicator led;
        QCOMPARE(led.colorFor(studio::LedIndicator::Status::Off), QColor("#484f58"));
    }

    void colorFor_Ok() {
        studio::LedIndicator led;
        QCOMPARE(led.colorFor(studio::LedIndicator::Status::Ok), QColor("#3fb950"));
    }

    void colorFor_Warn() {
        studio::LedIndicator led;
        QCOMPARE(led.colorFor(studio::LedIndicator::Status::Warn), QColor("#d29922"));
    }

    void colorFor_Error() {
        studio::LedIndicator led;
        QCOMPARE(led.colorFor(studio::LedIndicator::Status::Error), QColor("#f85149"));
    }

    void setStatus_roundtrip() {
        studio::LedIndicator led;
        led.setStatus(studio::LedIndicator::Status::Warn);
        QCOMPARE(led.status(), studio::LedIndicator::Status::Warn);
    }
};

QTEST_MAIN(TestLedIndicator)
#include "test_ledindicator.moc"
