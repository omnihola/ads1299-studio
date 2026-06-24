#include <QtTest/QtTest>

#include "core/device/Ads1299Registers.h"
#include "core/device/DeviceConfig.h"

using namespace studio;

class TestDeviceConfig : public QObject {
    Q_OBJECT

private slots:
    // Default DeviceConfig round-trips through register bytes unchanged.
    void defaultRoundTrips()
    {
        DeviceConfig d;
        const auto rt = DeviceConfig::fromRegisterBytes(d.toRegisterBytes());

        QCOMPARE(rt.sampleRate(),  d.sampleRate());
        QCOMPARE(rt.gain(),        d.gain());
        QCOMPARE(rt.mux(),         d.mux());
        QCOMPARE(rt.srb1(),        d.srb1());
        QCOMPARE(rt.biasEnabled(), d.biasEnabled());
    }

    // Field-level encoding correctness.
    void encodings()
    {
        QCOMPARE(static_cast<int>(regs::encodeGain(24)),      0b110);
        QCOMPARE(static_cast<int>(regs::encodeGain(1)),       0b000);
        QCOMPARE(static_cast<int>(regs::encodeSampleRate(250)),  0b110);
        QCOMPARE(static_cast<int>(regs::encodeSampleRate(16000)), 0b000);
    }

    // Default channel byte is 0x60 (gain 24, mux 0, PD=0).
    void chnsetByteForGain24MuxNormal()
    {
        DeviceConfig d;
        const auto bytes = d.toRegisterBytes();
        // CH1SET is at index 4 (register address 0x05).
        QCOMPARE(bytes[4], static_cast<uint8_t>(0x60));
    }

    // Mutated config round-trips all modeled fields.
    void mutatedRoundTrips()
    {
        const DeviceConfig original;
        const DeviceConfig modified = original
            .withSampleRate(500)
            .withGain(0, 1)
            .withMux(3, 5)   // test signal on ch3
            .withSrb1(true)
            .withBiasEnabled(false);

        const auto rt = DeviceConfig::fromRegisterBytes(modified.toRegisterBytes());

        QCOMPARE(rt.sampleRate(),    500);
        QCOMPARE(rt.gain()[0],       1);
        QCOMPARE(rt.mux()[3],        5);
        QCOMPARE(rt.srb1(),          true);
        QCOMPARE(rt.biasEnabled(),   false);
    }

    // with* methods do not mutate the original (immutability).
    void immutability()
    {
        DeviceConfig a;
        const auto b = a.withSampleRate(1000);
        QCOMPARE(a.sampleRate(), 500);
        QCOMPARE(b.sampleRate(), 1000);
    }

    // biasEnabled is decoded from the CONFIG3 PD_BIAS bit (bit 3), not an exact
    // byte match — so a real device read-back carrying other reserved/status bits
    // in CONFIG3 still parses correctly.
    void biasEnabledParsedFromBitNotExactByte()
    {
        auto bytes = DeviceConfig().withBiasEnabled(true).toRegisterBytes();
        // Enabled writer emits 0xEC (bit3 set). Set extra reserved/status bits.
        bytes[2] = 0xEDu;  // 0xEC | bit0
        QCOMPARE(DeviceConfig::fromRegisterBytes(bytes).biasEnabled(), true);
        bytes[2] = 0xFEu;  // many bits, bit3 still set
        QCOMPARE(DeviceConfig::fromRegisterBytes(bytes).biasEnabled(), true);

        // Disabled: bit3 clear, even with other bits set.
        bytes[2] = 0xE2u;  // 0xE0 | bit1, bit3 clear
        QCOMPARE(DeviceConfig::fromRegisterBytes(bytes).biasEnabled(), false);
        bytes[2] = 0xF7u;  // bit3 clear (0b11110111)
        QCOMPARE(DeviceConfig::fromRegisterBytes(bytes).biasEnabled(), false);
    }
};

QTEST_MAIN(TestDeviceConfig)
#include "test_deviceconfig.moc"
