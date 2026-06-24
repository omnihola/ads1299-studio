// tests/test_registerhexdump.cpp
//
// Unit tests for studio::registerHexDump().
// Pure function test — no widgets, no display needed.

#include <QtTest/QtTest>
#include "core/device/DeviceConfig.h"

class TestRegisterHexDump : public QObject
{
    Q_OBJECT

private slots:
    void defaultConfig_containsConfig1();
    void defaultConfig_containsCh1set();
    void defaultConfig_has23Tokens();
};

void TestRegisterHexDump::defaultConfig_containsConfig1()
{
    // Default DeviceConfig: 500 SPS → CONFIG1 = 0x90 | 0x05 = 0x95
    studio::DeviceConfig cfg;
    const QString dump = studio::registerHexDump(cfg.toRegisterBytes());
    QVERIFY2(dump.contains("CONFIG1=0x95"),
             qPrintable(QString("Expected CONFIG1=0x95 in: %1").arg(dump)));
}

void TestRegisterHexDump::defaultConfig_containsCh1set()
{
    // Default: gain=24 (code 0b110=6), mux=0 → CH1SET byte = (6<<4)|0 = 0x60
    studio::DeviceConfig cfg;
    const QString dump = studio::registerHexDump(cfg.toRegisterBytes());
    QVERIFY2(dump.contains("CH1SET=0x60"),
             qPrintable(QString("Expected CH1SET=0x60 in: %1").arg(dump)));
}

void TestRegisterHexDump::defaultConfig_has23Tokens()
{
    studio::DeviceConfig cfg;
    const QString dump = studio::registerHexDump(cfg.toRegisterBytes());
    const QStringList tokens = dump.split(' ', Qt::SkipEmptyParts);
    QCOMPARE(tokens.size(), 23);
}

QTEST_APPLESS_MAIN(TestRegisterHexDump)
#include "test_registerhexdump.moc"
