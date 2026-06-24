// tests/test_prerecordcheck.cpp
// TDD test for PreRecordCheckDialog static helpers and dialog construction.
// Run headless: QT_QPA_PLATFORM=offscreen

#include <QtTest/QtTest>
#include "ui/PreRecordCheckDialog.h"

class TestPreRecordCheck : public QObject
{
    Q_OBJECT

private slots:
    void okCountAllConnected()
    {
        QCOMPARE(studio::PreRecordCheckDialog::okChannelCount(0, 0), 8);
    }

    void okCountWithLeadoff()
    {
        // ch0, ch2 off via P (0b00000101); ch3 off via N (0b00001000)
        // Channels off: 0, 2, 3 → 5 remaining
        QCOMPARE(studio::PreRecordCheckDialog::okChannelCount(0b00000101, 0b00001000), 5);
    }

    void statusTextLists()
    {
        // 0b00000101 → ch0, ch2 lead-off P; 0b00001000 → ch3 lead-off N
        // 1-based names: CH1, CH3, CH4 are lead-off; 5 of 8 connected
        const QString txt = studio::PreRecordCheckDialog::channelStatusText(0b00000101, 0b00001000);
        QVERIFY2(txt.contains("5 of 8"), qPrintable(txt));
        QVERIFY2(txt.contains("CH1"),    qPrintable(txt));
        QVERIFY2(txt.contains("CH3"),    qPrintable(txt));
        QVERIFY2(txt.contains("CH4"),    qPrintable(txt));
    }

    void statusTextAllOk()
    {
        const QString txt = studio::PreRecordCheckDialog::channelStatusText(0, 0);
        QVERIFY2(txt.contains("8 of 8"), qPrintable(txt));
    }

    void dialogConstructs()
    {
        // Construct without crashing — do NOT call exec() (would block)
        studio::PreRecordCheckDialog d1(0, 0);
        studio::PreRecordCheckDialog d2(0xFF, 0xFF);
        QVERIFY(d1.children().count() > 0);
        QVERIFY(d2.children().count() > 0);
    }
};

QTEST_MAIN(TestPreRecordCheck)
#include "test_prerecordcheck.moc"
