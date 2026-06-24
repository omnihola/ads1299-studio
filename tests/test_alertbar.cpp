// tests/test_alertbar.cpp
//
// QtTest — TDD for AlertBar pure summarizer + widget-level signal routing.
// Build: compiled with AlertBar.cpp directly into this test target.
// Run with: QT_QPA_PLATFORM=offscreen ctest -R alertbar

#include <QtTest/QtTest>

#include "ui/AlertBar.h"
#include "app/SessionController.h"
#include "core/acquisition/SimulatedSource.h"

class TestAlertBar : public QObject
{
    Q_OBJECT

private slots:

    // ------------------------------------------------------------------
    // Pure summarizer — no widget needed
    // ------------------------------------------------------------------

    // error message takes priority over everything
    void errorHasPriority()
    {
        auto s = studio::AlertBar::summarize("USB read failed", 5, 0x01, 0x00);
        QCOMPARE(s.level, studio::AlertLevel::Error);
        QVERIFY2(s.text.contains("USB read failed"),
                 qPrintable(s.text));
    }

    // dropped samples → Error even without an error string
    void droppedIsError()
    {
        auto s = studio::AlertBar::summarize("", 42, 0, 0);
        QCOMPARE(s.level, studio::AlertLevel::Error);
        QVERIFY2(s.text.contains("42"),
                 qPrintable(s.text));
    }

    // lead-off → Warn; 1-based channel numbers: bits 0,2 from P and bit 3 from N
    // combined mask = 0b00000101 | 0b00001000 = bits 0,2,3 → CH1, CH3, CH4
    void leadOffIsWarn()
    {
        auto s = studio::AlertBar::summarize("", 0,
            /*leadOffP=*/0b00000101,
            /*leadOffN=*/0b00001000);
        QCOMPARE(s.level, studio::AlertLevel::Warn);
        QVERIFY2(s.text.contains("CH1"), qPrintable(s.text));
        QVERIFY2(s.text.contains("CH3"), qPrintable(s.text));
        QVERIFY2(s.text.contains("CH4"), qPrintable(s.text));
    }

    // everything clean → Ok
    void allOk()
    {
        auto s = studio::AlertBar::summarize("", 0, 0, 0);
        QCOMPARE(s.level, studio::AlertLevel::Ok);
        QCOMPARE(s.text, QString("Signal OK"));
    }

    // ------------------------------------------------------------------
    // Widget-level: applyForTest drives the same store+refresh path
    // ------------------------------------------------------------------

    void widgetReflectsMetrics()
    {
        studio::SessionController ctrl(new studio::SimulatedSource(1));
        studio::AlertBar bar(&ctrl);

        // Drive via the test hook: 3 dropped samples, no error, no lead-off
        bar.applyForTest("", 3, 0, 0);

        QCOMPARE(bar.currentSummary().level, studio::AlertLevel::Error);
        QVERIFY2(bar.currentSummary().text.contains("3"),
                 qPrintable(bar.currentSummary().text));
    }
};

QTEST_MAIN(TestAlertBar)
#include "test_alertbar.moc"
