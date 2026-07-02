#include "DeviceConfig.h"
#include "Ads1299Registers.h"

#include <QJsonArray>
#include <stdexcept>
#include <array>

namespace studio {

// ---------------------------------------------------------------------------
// Construction / defaults
// ---------------------------------------------------------------------------

DeviceConfig::DeviceConfig()
    : m_sampleRate(500)
    , m_srb1(false)
    , m_biasEnabled(true)
{
    m_gain.fill(24);
    m_mux.fill(0);
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

int                  DeviceConfig::sampleRate()  const { return m_sampleRate; }
std::array<int, 8>   DeviceConfig::gain()        const { return m_gain; }
std::array<int, 8>   DeviceConfig::mux()         const { return m_mux; }
bool                 DeviceConfig::srb1()         const { return m_srb1; }
bool                 DeviceConfig::biasEnabled()  const { return m_biasEnabled; }
bool                 DeviceConfig::internalTestSignal() const { return m_testSignal; }

// ---------------------------------------------------------------------------
// Immutable with* mutators
// ---------------------------------------------------------------------------

DeviceConfig DeviceConfig::withSampleRate(int sps) const
{
    DeviceConfig copy = *this;
    copy.m_sampleRate = sps;
    return copy;
}

DeviceConfig DeviceConfig::withGain(int channel, int gainValue) const
{
    if (channel < 0 || channel > 7)
        throw std::out_of_range("DeviceConfig::withGain: channel must be 0..7");
    DeviceConfig copy = *this;
    copy.m_gain[static_cast<std::size_t>(channel)] = gainValue;
    return copy;
}

DeviceConfig DeviceConfig::withMux(int channel, int muxMode) const
{
    if (channel < 0 || channel > 7)
        throw std::out_of_range("DeviceConfig::withMux: channel must be 0..7");
    DeviceConfig copy = *this;
    copy.m_mux[static_cast<std::size_t>(channel)] = muxMode;
    return copy;
}

DeviceConfig DeviceConfig::withSrb1(bool enabled) const
{
    DeviceConfig copy = *this;
    copy.m_srb1 = enabled;
    return copy;
}

DeviceConfig DeviceConfig::withBiasEnabled(bool enabled) const
{
    DeviceConfig copy = *this;
    copy.m_biasEnabled = enabled;
    return copy;
}

DeviceConfig DeviceConfig::withInternalTestSignal(bool enabled) const
{
    DeviceConfig copy = *this;
    copy.m_testSignal = enabled;
    return copy;
}

// ---------------------------------------------------------------------------
// Register encoding
// ---------------------------------------------------------------------------

// Register address layout: index i = register address (0x01 + i)
// toRegisterBytes() fills all 23 bytes; non-modeled bytes get sane defaults.
std::array<uint8_t, 23> DeviceConfig::toRegisterBytes() const
{
    std::array<uint8_t, 23> bytes{};
    bytes.fill(0x00);

    // index 0 → CONFIG1 (0x01): 0x90 | DR code
    bytes[0] = static_cast<uint8_t>(0x90u | regs::encodeSampleRate(m_sampleRate));

    // index 1 → CONFIG2 (0x02): 0xC0, +INT_CAL (0x10) for the internal
    // test signal (verified on hardware with 0xD0)
    bytes[1] = m_testSignal ? 0xD0u : 0xC0u;

    // index 2 → CONFIG3 (0x03): biasEnabled -> 0xEC (PD_BIAS + BIASREF_INT set), else 0xE0
    bytes[2] = m_biasEnabled ? 0xECu : 0xE0u;

    // index 3 → LOFF (0x04): 0x00
    bytes[3] = 0x00u;

    // indices 4..11 → CH1SET..CH8SET (0x05..0x0C)
    for (int ch = 0; ch < 8; ++ch) {
        const uint8_t gainCode = regs::encodeGain(m_gain[static_cast<std::size_t>(ch)]);
        const uint8_t muxCode  = regs::encodeMux(m_mux[static_cast<std::size_t>(ch)]);
        // PD=0 (bit7=0), GAIN in bits[6:4], reserved bit3=0, MUX in bits[2:0]
        bytes[4 + static_cast<std::size_t>(ch)] = static_cast<uint8_t>((gainCode << 4u) | muxCode);
    }

    // indices 12..19 → BIAS_SENSP, BIAS_SENSN, LOFF_SENSP, LOFF_SENSN,
    //                   LOFF_FLIP, LOFF_STATP, LOFF_STATN, GPIO: 0x00
    // (already zero-filled)

    // index 20 → MISC1 (0x15): bit5 = SRB1
    bytes[20] = m_srb1 ? 0x20u : 0x00u;

    // index 21 → MISC2 (0x16): 0x00
    // index 22 → CONFIG4 (0x17): 0x00
    // (already zero-filled)

    return bytes;
}

// ---------------------------------------------------------------------------
// Register decoding
// ---------------------------------------------------------------------------

DeviceConfig DeviceConfig::fromRegisterBytes(const std::array<uint8_t, 23>& bytes)
{
    DeviceConfig cfg;

    // CONFIG1 (index 0): decode DR[2:0]
    cfg.m_sampleRate = regs::decodeSampleRate(bytes[0]);

    // CONFIG2 (index 1): INT_CAL bit (bit 4) = internal test signal.
    cfg.m_testSignal = (bytes[1] & 0x10u) != 0u;

    // CONFIG3 (index 2): biasEnabled tracks the PD_BIAS bit (bit 3). Test the bit
    // rather than the exact byte so a real device's CONFIG3 read-back (which may
    // carry other reserved/status bits) still parses correctly. Our writer emits
    // 0xEC (bit3=1) when enabled and 0xE0 (bit3=0) when disabled, so this round-trips.
    cfg.m_biasEnabled = (bytes[2] & 0x08u) != 0u;

    // CH1SET..CH8SET (indices 4..11)
    for (int ch = 0; ch < 8; ++ch) {
        const uint8_t b = bytes[4 + static_cast<std::size_t>(ch)];
        cfg.m_gain[static_cast<std::size_t>(ch)] = regs::decodeGain(b);
        cfg.m_mux[static_cast<std::size_t>(ch)]  = regs::decodeMux(b);
    }

    // MISC1 (index 20): bit5 = SRB1
    cfg.m_srb1 = (bytes[20] & 0x20u) != 0u;

    return cfg;
}

// ---------------------------------------------------------------------------
// JSON serialisation (metadata / provenance)
// ---------------------------------------------------------------------------

QJsonObject DeviceConfig::toJson() const
{
    QJsonObject obj;
    obj["sampleRate"] = m_sampleRate;
    obj["srb1"]       = m_srb1;
    obj["biasEnabled"] = m_biasEnabled;
    obj["testSignal"]  = m_testSignal;

    QJsonArray gainArr;
    QJsonArray muxArr;
    for (int ch = 0; ch < 8; ++ch) {
        gainArr.append(m_gain[static_cast<std::size_t>(ch)]);
        muxArr.append(m_mux[static_cast<std::size_t>(ch)]);
    }
    obj["gain"] = gainArr;
    obj["mux"]  = muxArr;

    return obj;
}

// ---------------------------------------------------------------------------
// registerHexDump — free function
// ---------------------------------------------------------------------------

QString registerHexDump(const std::array<uint8_t, 23>& bytes)
{
    // The 23 registers are at addresses 0x01..0x17 (inclusive).
    // Register names in address order:
    static const char* const kNames[23] = {
        "CONFIG1", "CONFIG2", "CONFIG3", "LOFF",
        "CH1SET",  "CH2SET",  "CH3SET",  "CH4SET",
        "CH5SET",  "CH6SET",  "CH7SET",  "CH8SET",
        "BIAS_SENSP", "BIAS_SENSN", "LOFF_SENSP", "LOFF_SENSN",
        "LOFF_FLIP", "LOFF_STATP", "LOFF_STATN", "GPIO",
        "MISC1", "MISC2", "CONFIG4"
    };

    QStringList tokens;
    tokens.reserve(23);
    for (int i = 0; i < 23; ++i) {
        const uint val = static_cast<uint>(bytes[static_cast<std::size_t>(i)]);
        // Format: NAME=0xHH  (hex always 2 uppercase digits)
        const QString hex = QString::number(val, 16).rightJustified(2, QLatin1Char('0')).toUpper();
        tokens.append(QString("%1=0x%2").arg(QLatin1String(kNames[i])).arg(hex));
    }
    return tokens.join(QLatin1Char(' '));
}

} // namespace studio
