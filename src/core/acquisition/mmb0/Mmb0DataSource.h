// src/core/acquisition/mmb0/Mmb0DataSource.h
#pragma once

#include "core/acquisition/IDataSource.h"
#include "core/acquisition/mmb0/ITransport.h"
#include "core/acquisition/mmb0/Ads1299RecordParser.h"
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
class Mmb0DataSource : public studio::IDataSource {
    Q_OBJECT

public:
    explicit Mmb0DataSource(studio::styx::ITransport* transport,
                            QObject* parent = nullptr);
    ~Mmb0DataSource() override;

    /// Set the register configuration to push to the device on start().
    void setConfig(const studio::DeviceConfig& cfg);

    /// Set blocksize (number of 27-byte records per /data read).
    void setBlocksizeSamples(int n);

    // ── IDataSource ──────────────────────────────────────────────────────────
    void start() override;
    void stop()  override;
    bool isRunning() const override { return running_; }
    studio::SourceCapabilities capabilities() const override;
    void setSampleRate(int sps) override;

private slots:
    void pollOnce();

private:
    /// Connect 9P session, write registers + blocksize, open /data, write acquire=1.
    /// Returns false and emits errorOccurred on any failure.
    bool bringUp();

    /// Write acquire=0 (best-effort) and clunk the data fid.
    void tearDown();

    // ── Register index → node name map ──────────────────────────────────────
    // Returns empty string for indices that are not modelled (should be skipped).
    static QString regIndexToName(int index);

    // ── State ────────────────────────────────────────────────────────────────
    studio::styx::ITransport*   transport_;   // owned
    studio::styx::Styx9pClient* client_ = nullptr;
    studio::DeviceConfig        config_;
    studio::mmb0::Ads1299RecordParser parser_;
    int      blocksizeSamples_ = 30;
    bool     running_          = false;
    uint32_t dataFid_          = 0;
    QTimer*  pollTimer_        = nullptr; // new QTimer(this) in ctor — migrates with moveToThread
};

} // namespace studio::mmb0
