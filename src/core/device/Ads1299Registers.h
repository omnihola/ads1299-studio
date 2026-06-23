#pragma once
#include <cstdint>
#include <stdexcept>

namespace studio {

/// ADS1299 register addresses (datasheet SBAS499)
namespace Reg {
    static constexpr uint8_t ID          = 0x00;
    static constexpr uint8_t CONFIG1     = 0x01;
    static constexpr uint8_t CONFIG2     = 0x02;
    static constexpr uint8_t CONFIG3     = 0x03;
    static constexpr uint8_t LOFF        = 0x04;
    static constexpr uint8_t CH1SET      = 0x05;
    static constexpr uint8_t CH2SET      = 0x06;
    static constexpr uint8_t CH3SET      = 0x07;
    static constexpr uint8_t CH4SET      = 0x08;
    static constexpr uint8_t CH5SET      = 0x09;
    static constexpr uint8_t CH6SET      = 0x0A;
    static constexpr uint8_t CH7SET      = 0x0B;
    static constexpr uint8_t CH8SET      = 0x0C;
    static constexpr uint8_t BIAS_SENSP  = 0x0D;
    static constexpr uint8_t BIAS_SENSN  = 0x0E;
    static constexpr uint8_t LOFF_SENSP  = 0x0F;
    static constexpr uint8_t LOFF_SENSN  = 0x10;
    static constexpr uint8_t LOFF_FLIP   = 0x11;
    static constexpr uint8_t LOFF_STATP  = 0x12;
    static constexpr uint8_t LOFF_STATN  = 0x13;
    static constexpr uint8_t GPIO        = 0x14;
    static constexpr uint8_t MISC1       = 0x15;
    static constexpr uint8_t MISC2       = 0x16;
    static constexpr uint8_t CONFIG4     = 0x17;
} // namespace Reg

/// Field-level encode/decode helpers
namespace regs {

/// Encode sample rate (SPS) to CONFIG1 DR[2:0] code.
/// Throws std::invalid_argument for unsupported values.
inline uint8_t encodeSampleRate(int sps)
{
    switch (sps) {
        case 16000: return 0b000;
        case  8000: return 0b001;
        case  4000: return 0b010;
        case  2000: return 0b011;
        case  1000: return 0b100;
        case   500: return 0b101;
        case   250: return 0b110;
        default:
            throw std::invalid_argument("Unsupported sample rate");
    }
}

/// Decode CONFIG1 byte -> SPS.  Returns -1 for codes >= 7 (undefined).
inline int decodeSampleRate(uint8_t config1)
{
    const uint8_t dr = config1 & 0x07u;
    switch (dr) {
        case 0b000: return 16000;
        case 0b001: return  8000;
        case 0b010: return  4000;
        case 0b011: return  2000;
        case 0b100: return  1000;
        case 0b101: return   500;
        case 0b110: return   250;
        default:    return    -1;
    }
}

/// Encode gain value to CHnSET GAIN[6:4] 3-bit code.
/// Throws std::invalid_argument for unsupported values.
inline uint8_t encodeGain(int gain)
{
    switch (gain) {
        case  1: return 0b000;
        case  2: return 0b001;
        case  4: return 0b010;
        case  6: return 0b011;
        case  8: return 0b100;
        case 12: return 0b101;
        case 24: return 0b110;
        default:
            throw std::invalid_argument("Unsupported gain");
    }
}

/// Decode CHnSET byte -> gain value.
inline int decodeGain(uint8_t chnset)
{
    const uint8_t code = (chnset >> 4u) & 0x07u;
    switch (code) {
        case 0b000: return  1;
        case 0b001: return  2;
        case 0b010: return  4;
        case 0b011: return  6;
        case 0b100: return  8;
        case 0b101: return 12;
        case 0b110: return 24;
        default:    return -1;
    }
}

/// Encode MUX mode (identity 0..7, clamped to [0,7]).
inline uint8_t encodeMux(int muxMode)
{
    if (muxMode < 0) return 0u;
    if (muxMode > 7) return 7u;
    return static_cast<uint8_t>(muxMode);
}

/// Decode CHnSET byte -> MUX[2:0].
inline int decodeMux(uint8_t chnset)
{
    return static_cast<int>(chnset & 0x07u);
}

} // namespace regs
} // namespace studio
