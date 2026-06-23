#pragma once
#include <array>
#include <cstdint>
#include <QJsonObject>
#include <QMetaType>
#include <QString>

namespace studio {

/// Immutable high-level configuration of an ADS1299 device.
///
/// Defaults: 250 SPS, all channels gain=24 mux=0 (normal electrode),
/// srb1=false, biasEnabled=true.
///
/// with* methods return a new copy with the requested field changed;
/// the original is never modified (immutable value-object pattern).
class DeviceConfig {
public:
    DeviceConfig();

    int                  sampleRate()   const;
    std::array<int, 8>   gain()         const;
    std::array<int, 8>   mux()          const;
    bool                 srb1()         const;
    bool                 biasEnabled()  const;

    DeviceConfig withSampleRate(int sps)                  const;
    DeviceConfig withGain(int channel, int gainValue)     const;  ///< channel 0-based
    DeviceConfig withMux(int channel, int muxMode)        const;  ///< channel 0-based
    DeviceConfig withSrb1(bool enabled)                   const;
    DeviceConfig withBiasEnabled(bool enabled)            const;

    /// Returns the 23-byte writable register block CONFIG1(0x01)..CONFIG4(0x17).
    /// Index 0 corresponds to register address 0x01.
    std::array<uint8_t, 23> toRegisterBytes() const;

    /// Reconstruct a DeviceConfig from a register byte block.
    static DeviceConfig fromRegisterBytes(const std::array<uint8_t, 23>& bytes);

    QJsonObject toJson() const;

private:
    int                m_sampleRate;
    std::array<int, 8> m_gain;
    std::array<int, 8> m_mux;
    bool               m_srb1;
    bool               m_biasEnabled;
};

/// Free function: format a 23-byte register block as a human-readable hex string.
/// Returns e.g. "CONFIG1=0x96 CONFIG2=0xC0 CONFIG3=0xEC ..." (space-separated,
/// 23 tokens, one per register 0x01..0x17).
QString registerHexDump(const std::array<uint8_t, 23>& bytes);

} // namespace studio

Q_DECLARE_METATYPE(studio::DeviceConfig)
