// src/core/acquisition/mmb0/Mmb0DataSource.h
#pragma once

#include "core/acquisition/IDataSource.h"
#include "core/acquisition/mmb0/ITransport.h"
#include "core/acquisition/mmb0/Ads1299WordParser.h"
#include "core/device/DeviceConfig.h"
#include <QTimer>
#include <cstdint>

namespace studio::styx {
class Styx9pClient;
}

namespace studio::mmb0 {

/// Real MMB0 acquisition source.
/// Takes ownership of the supplied ITransport (deleted in dtor).
/// Configure via setConfig() / setBlocksizeSamples() BEFORE start().
/// Threading: the owner is expected to moveToThread() this object onto
/// a worker thread; the internal QTimer and Styx I/O will run there.
///
/// Acquisition model (VERIFIED against the PDK firmware, 2026-07-01 handoff):
/// /ads1299evm/acquire is ONE-SHOT — writing "1" collects exactly one block of
/// blocksize_samples 32-bit words, then acquire auto-resets to "0". The source
/// therefore runs a poll loop per block:
///   write acquire="1" → poll-read acquire until "0" → read /data → re-arm.
class Mmb0DataSource : public studio::IDataSource {
    Q_OBJECT

public:
    explicit Mmb0DataSource(studio::styx::ITransport* transport,
                            QObject* parent = nullptr);
    ~Mmb0DataSource() override;

    /// Set the register configuration to push to the device on start().
    void setConfig(const studio::DeviceConfig& cfg);

    /// Set blocksize in EEG SAMPLES per block. The device file
    /// /ads1299evm/blocksize_samples counts 32-bit WORDS (9 per sample), so
    /// n*9 is written to the device.
    void setBlocksizeSamples(int n);

    /// How long acquire may stay at "1" before we declare the block dead and
    /// emit a hardware-pointing error (DRDY never fired). Default 4000 ms.
    void setAcquireTimeoutMs(int ms);

    // ── IDataSource ──────────────────────────────────────────────────────────
    void start() override;
    void stop()  override;
    bool isRunning() const override { return running_; }
    studio::SourceCapabilities capabilities() const override;
    void setSampleRate(int sps) override;

private slots:
    void pollOnce();

private:
    /// Attach the 9P session (no Tversion — the estyx firmware is attached
    /// directly with uname/aname "nobody"), push registers + blocksize, and
    /// arm the first block (acquire="1").
    /// Returns false and emits errorOccurred on any failure.
    bool bringUp();

    /// Write acquire="0" (best-effort) so the device is parked.
    void tearDown();

    void failAndStop(const QString& message);

    // ── Register index → node name map ──────────────────────────────────────
    // Returns empty string for indices that are not modelled (skipped).
    // NOTE: /ads1299evm/conf/reset is deliberately absent — writing it hangs
    // the Styx server until a power cycle (verified on hardware).
    static QString regIndexToName(int index);

    // ── State ────────────────────────────────────────────────────────────────
    studio::styx::ITransport*   transport_;   // owned
    studio::styx::Styx9pClient* client_ = nullptr;
    studio::DeviceConfig        config_;
    // Word order VERIFIED on hardware (raw dump 2026-07-02, internal test
    // signal): 9 big-endian 32-bit words per sample, 24-bit value
    // sign-extended (status 0xFFC00000, channels 0x000141xx ≈ +82k codes,
    // matching the ±1.875 mV test signal at gain 24 within 2%).
    Ads1299WordParser           parser_{Ads1299WordParser::WordOrder::BigEndian};
    // 32 samples = 288 words — the block size verified to sustain multi-block
    // streaming on hardware (also divides the firmware's DMA frame math well).
    int      blocksizeSamples_ = 32;    // EEG samples per block (x9 words on device)
    int      acquireTimeoutMs_ = 4000;
    int      msWaitingForBlock_ = 0;    // elapsed since the last acquire="1"
    bool     running_          = false;
    bool     acquireWedged_    = false; // block never completed; do not re-arm
    QTimer*  pollTimer_        = nullptr; // new QTimer(this) in ctor — migrates with moveToThread
};

} // namespace studio::mmb0
