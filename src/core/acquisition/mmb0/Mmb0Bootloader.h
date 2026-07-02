#pragma once

#include <QString>
#include <QStringList>
#include <cstdint>

namespace studio::mmb0 {

/// Firmware-upload path for the MMB0 (ADS1299EEGFE-PDK motherboard).
///
/// The board cold-boots as the TMS320C5000 ROM USB bootloader (0451:9001) on
/// EVERY power cycle; only after the firmware image is uploaded does it
/// re-enumerate as 0451:5718 (USBStyx mode) where the Styx/9P transport lives.
///
/// Upload protocol (verified on hardware against dsploader.py::loadDSP):
///   open → set_configuration(1) → claim_interface(0) →
///   set_interface_alt_setting(0,0) → ONE bulk transfer of the whole image to
///   ep 0x06 (timeout ~5 s) → release. No ZLP (79248 % 64 = 16 → the transfer
///   ends on a natural short packet). The DSP then self-resets.
///
/// The known-good image is ads1299evm-pdk.bin, 79248 bytes,
/// sha256 b5c1bc94… (see kKnownFirmwareSha256). It is NOT shipped with this
/// repository (TI licensing) — the user points the app at their copy via
/// QSettings key "mmb0/firmwarePath", the ADS1299_FIRMWARE environment
/// variable, or a firmware/ directory next to the executable.
class Mmb0Bootloader {
public:
    static constexpr uint16_t kVid           = 0x0451;
    static constexpr uint16_t kBootloaderPid = 0x9001;
    static constexpr uint16_t kStyxPid       = 0x5718;
    static constexpr uint8_t  kUploadEp      = 0x06;   // OUT bulk, 64 B packets

    static constexpr qint64 kKnownFirmwareSize = 79248;
    static const char*      kKnownFirmwareSha256;

    enum class FirmwareStatus {
        Ok,            ///< exact known release image (size + sha256 match)
        Missing,       ///< no file at the given path
        Unreadable,    ///< exists but cannot be opened/read
        Empty,         ///< zero bytes
        UnknownImage,  ///< readable but not the known image — upload at own risk
    };

    /// First candidate that exists as a readable regular file; empty if none.
    static QString locateFirmware(const QStringList& candidates);

    /// Candidate list assembled from QSettings("mmb0/firmwarePath"), the
    /// ADS1299_FIRMWARE environment variable, and conventional locations
    /// (firmware/ next to the executable and in the working directory).
    static QStringList defaultCandidates();

    /// Classify the file at @p path against the known release image.
    /// @p detail (optional) receives a human-readable explanation.
    static FirmwareStatus validateFirmware(const QString& path, QString* detail = nullptr);

    /// True if a 0451:9001 bootloader-mode device is currently enumerated.
    static bool isBootloaderPresent();

    /// True if a 0451:5718 Styx-mode device is currently enumerated.
    static bool isStyxPresent();

    /// Bulk-write the whole image to ep 0x06 in one transfer.
    /// Returns false and sets lastError() on failure.
    bool uploadFirmware(const QString& path);

    /// Poll until the 5718 Styx-mode device enumerates (the DSP self-resets
    /// and re-enumerates after a successful upload, typically within ~2 s).
    bool waitForStyx(int timeoutMs = 8000);

    QString lastError() const;

private:
    QString lastError_;
};

} // namespace studio::mmb0
