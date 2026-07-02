// src/core/acquisition/mmb0/Mmb0DataSource.cpp
#include "core/acquisition/mmb0/Mmb0DataSource.h"
#include "core/acquisition/mmb0/Styx9pClient.h"
#include "core/acquisition/SourceCapabilities.h"
#include <QDebug>
#include <QThread>
#include <QTimer>

namespace studio::mmb0 {

namespace {

const QString kAcquirePath   = QStringLiteral("/ads1299evm/acquire");
const QString kDataPath      = QStringLiteral("/ads1299evm/data");
const QString kBlocksizePath = QStringLiteral("/ads1299evm/blocksize_samples");
const QString kConfPrefix    = QStringLiteral("/ads1299evm/conf/");

// Poll cadence for the one-shot acquire loop. The verified reference client
// polled every 20 ms; each poll costs a walk/open/read/clunk round-trip, so
// don't go much faster.
constexpr int kPollIntervalMs = 20;

// Cap for a single /data Tread. The estyx server's message buffer is ~2 KB:
// requesting more returns garbage past word ~509 while still declaring the
// full count (verified on hardware 2026-07-02 — a one-shot 2304-byte read of
// a 576-word block was junk after 509 words; ≤1152-byte reads were clean).
// 2016 = 56 samples × 36 bytes keeps every chunk sample-aligned and safely
// under the (2048 − 11-byte Rread header) payload ceiling.
constexpr uint32_t kMaxDataChunkBytes = 2016;

// estyx u32 conf files take hex text ("0x96"); bool files take text "1"/"0".
// Both formats verified with read-back on real hardware.
QByteArray toHexText(uint8_t v)
{
    return QByteArray("0x")
         + QByteArray::number(v, 16).rightJustified(2, '0').toUpper();
}

} // namespace

// ── Static helpers ────────────────────────────────────────────────────────────

// Maps toRegisterBytes() array index → 9P conf node name.
// Returns empty string for indices that are not modelled (skip them).
// LOFF_STATP/LOFF_STATN (17/18) are read-only; MISC2 (21) is untouched.
// "reset" is intentionally NOT mapped — writing it hangs the Styx server.
/*static*/ QString Mmb0DataSource::regIndexToName(int index) {
    switch (index) {
    case  0: return QStringLiteral("config1");
    case  1: return QStringLiteral("config2");
    case  2: return QStringLiteral("config3");
    case  3: return QStringLiteral("loff");
    case  4: return QStringLiteral("ch1set");
    case  5: return QStringLiteral("ch2set");
    case  6: return QStringLiteral("ch3set");
    case  7: return QStringLiteral("ch4set");
    case  8: return QStringLiteral("ch5set");
    case  9: return QStringLiteral("ch6set");
    case 10: return QStringLiteral("ch7set");
    case 11: return QStringLiteral("ch8set");
    case 12: return QStringLiteral("biassensp");
    case 13: return QStringLiteral("biassensn");
    case 14: return QStringLiteral("loffsensp");
    case 15: return QStringLiteral("loffsensn");
    case 16: return QStringLiteral("loffflip");
    case 19: return QStringLiteral("gpio");
    case 20: return QStringLiteral("misc1");
    case 22: return QStringLiteral("config4");
    default: return QString(); // not modelled — skip
    }
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

Mmb0DataSource::Mmb0DataSource(studio::styx::ITransport* transport,
                               QObject* parent)
    : IDataSource(parent)
    , transport_(transport)
{
    // MUST be a pointer member created with `this` so it migrates with moveToThread().
    pollTimer_ = new QTimer(this);
    pollTimer_->setSingleShot(false);
    pollTimer_->setInterval(kPollIntervalMs);
    connect(pollTimer_, &QTimer::timeout, this, &Mmb0DataSource::pollOnce);
}

Mmb0DataSource::~Mmb0DataSource() {
    if (running_) {
        running_ = false;
        pollTimer_->stop();
        tearDown();
    }
    delete client_;
    delete transport_;
}

// ── Configuration ─────────────────────────────────────────────────────────────

void Mmb0DataSource::setConfig(const studio::DeviceConfig& cfg) {
    config_ = cfg;
}

void Mmb0DataSource::setBlocksizeSamples(int n) {
    if (n > 0) blocksizeSamples_ = n;
}

void Mmb0DataSource::setAcquireTimeoutMs(int ms) {
    if (ms > 0) acquireTimeoutMs_ = ms;
}

void Mmb0DataSource::setSampleRate(int sps) {
    config_ = config_.withSampleRate(sps);
}

// ── IDataSource ───────────────────────────────────────────────────────────────

studio::SourceCapabilities Mmb0DataSource::capabilities() const {
    studio::SourceCapabilities c;
    c.name        = QStringLiteral("ADS1299 (MMB0)");
    c.channels    = 8;
    c.sampleRates = {250, 500, 1000, 2000, 4000, 8000, 16000};
    return c;
}

void Mmb0DataSource::start() {
    if (running_) return;

    parser_.reset();
    acquireWedged_ = false;

    if (!bringUp()) {
        emit runningChanged(false);
        return;
    }

    running_ = true;
    emit runningChanged(true);
    pollTimer_->start();
}

void Mmb0DataSource::stop() {
    if (!running_) return;

    running_ = false;
    pollTimer_->stop();
    tearDown();
    emit runningChanged(false);
}

// ── pollOnce ──────────────────────────────────────────────────────────────────
// One-shot block loop: acquire auto-resets to "0" when the firmware finished
// collecting a block (DRDY-driven DMA). Until then /data has nothing for us.

void Mmb0DataSource::pollOnce() {
    if (!running_) return;

    // 1. Has the current block completed? (acquire reads back "0")
    QByteArray state;
    if (!client_->readPath(kAcquirePath, state, 16)) {
        failAndStop(QStringLiteral("Read of acquire failed: ") + client_->lastError());
        return;
    }
    if (!state.trimmed().startsWith('0')) {
        msWaitingForBlock_ += pollTimer_->interval();
        if (msWaitingForBlock_ >= acquireTimeoutMs_) {
            // The block never completed (DRDY chain dead, or the firmware's
            // fragile cross-block DMA bookkeeping dropped a completion).
            // Leave acquire at "1" as evidence: the firmware's readblock
            // refuses re-arming with interrupts globally disabled
            // (t1299_ob.c XFERPROG early-return), so writing "1" again in
            // this state bricks the USB stack until a power cycle.
            acquireWedged_ = true;
            failAndStop(QStringLiteral(
                "Acquisition block never completed (acquire stuck at 1; DRDY did not finish the block). "
                "If this repeats, power-cycle the board; also check the "
                "ADS1299EEGFE front-end seating and CLKSEL jumpers."));
        }
        return;
    }

    // 2. Block collected — read it from /data (blocksize words × 4 bytes),
    //    in chunks no larger than the estyx server's message buffer.
    const int blockBytes = blocksizeSamples_ * Ads1299WordParser::kBytesPerSample;
    QByteArray block;
    if (!client_->readPath(kDataPath, block, blockBytes, kMaxDataChunkBytes)) {
        failAndStop(QStringLiteral("Read from /data failed: ") + client_->lastError());
        return;
    }
    if (block.size() != blockBytes) {
        failAndStop(QStringLiteral("Short /data block: expected %1 bytes, got %2 bytes")
                        .arg(blockBytes)
                        .arg(block.size()));
        return;
    }

    if (!block.isEmpty()) {
        // A completed block should be whole 9-word samples. Feed only the
        // aligned prefix — a ragged tail would permanently shift the
        // status/channel word boundaries for every later sample.
        const int aligned =
            block.size() - (block.size() % Ads1299WordParser::kBytesPerSample);
        if (aligned != block.size()) {
            qWarning() << "[Mmb0DataSource] ragged /data block:" << block.size()
                       << "bytes; dropping" << (block.size() - aligned)
                       << "tail bytes to keep sample alignment";
        }
        parser_.feed(block.left(aligned));
        QVector<studio::EegFrame> frames = parser_.takeFrames();
        if (!frames.empty()) {
            studio::EegFrameBatch batch(frames.begin(), frames.end());
            emit framesReady(batch);
        }
    }

    // 3. Re-arm the next one-shot block.
    // NOTE (one-shot firmware model): the ADC output between block completion
    // and this re-arm is not captured, and parser seq numbers stay contiguous
    // across that gap — SessionController's seq-gap detection cannot see
    // inter-block losses. Gap-free capture would need firmware support.
    if (!client_->writePath(kAcquirePath, QByteArrayLiteral("1"))) {
        failAndStop(QStringLiteral("Failed to re-arm acquire: ") + client_->lastError());
        return;
    }
    msWaitingForBlock_ = 0;
}

// ── bringUp ──────────────────────────────────────────────────────────────────

bool Mmb0DataSource::bringUp() {
    // (Re)create the client so start() can be called repeatedly after stop().
    delete client_;
    client_ = new studio::styx::Styx9pClient(transport_, 3000);

    // No Tversion: the reference client (and the working probe tools) attach
    // directly. uname/aname "nobody" verified on hardware.
    if (!client_->attach(QStringLiteral("nobody"), QStringLiteral("nobody"))) {
        emit errorOccurred(QStringLiteral("attach failed: ") + client_->lastError());
        return false;
    }

    // Drain any in-flight one-shot block from a previous session before
    // touching registers: the chip stays in RDATAC until the firmware's
    // block-complete ISR issues STOP+SDATAC, and WREGs are silently ignored
    // in RDATAC (observed live 2026-07-02: a fast stop→start left channels
    // on their previous MUX). acquire auto-resets to "0" once it settles.
    //
    // If acquire NEVER settles, the firmware has a dead block whose
    // iXferInProgress flag was never cleared. Re-arming in that state trips
    // ADS1299_readblock's XFERPROG early-return, which leaves global
    // interrupts disabled — the whole USB stack dies until a power cycle
    // (verified live 2026-07-02). Refuse to arm instead.
    {
        QByteArray st;
        bool settled = false;
        for (int waited = 0; waited <= acquireTimeoutMs_; waited += 50) {
            if (!client_->readPath(kAcquirePath, st, 16))
                break;   // let the register writes surface the real error
            if (st.trimmed().startsWith('0')) {
                settled = true;
                break;
            }
            QThread::msleep(50);
        }
        if (!settled && st.trimmed().startsWith('1')) {
            emit errorOccurred(QStringLiteral(
                "A previous acquisition never completed and the firmware "
                "cannot be safely re-armed — power-cycle the MMB0 board "
                "(unplug USB and power, wait a few seconds, reconnect)."));
            return false;
        }
    }

    // Write each modelled register as hex text (estyx u32 file format).
    const auto regs = config_.toRegisterBytes();
    for (int i = 0; i < int(regs.size()); ++i) {
        const QString name = regIndexToName(i);
        if (name.isEmpty()) continue;
        const QString path = kConfPrefix + name;
        if (!client_->writePath(path, toHexText(regs[i]))) {
            emit errorOccurred(QStringLiteral("Failed to write ") + path
                               + QStringLiteral(": ") + client_->lastError());
            return false;
        }
    }

    // Write blocksize in device units (32-bit words, decimal text).
    {
        const int words = blocksizeSamples_ * Ads1299WordParser::kWordsPerSample;
        if (!client_->writePath(kBlocksizePath, QByteArray::number(words))) {
            emit errorOccurred(QStringLiteral("Failed to write blocksize_samples: ")
                               + client_->lastError());
            return false;
        }
    }

    // Arm the first one-shot block.
    if (!client_->writePath(kAcquirePath, QByteArrayLiteral("1"))) {
        emit errorOccurred(QStringLiteral("Failed to write acquire=1: ") + client_->lastError());
        return false;
    }
    msWaitingForBlock_ = 0;

    return true;
}

// ── tearDown ──────────────────────────────────────────────────────────────────

void Mmb0DataSource::tearDown() {
    if (!client_) return;

    // After a wedged (never-completed) block, leave acquire at "1" so the
    // next bringUp's drain step can detect the dead state and refuse to
    // re-arm (see bringUp). Parking it to "0" would hide the evidence and
    // the next arm would brick the firmware's USB stack.
    if (acquireWedged_) return;

    // Best-effort: park the device (bool file, text "0").
    client_->writePath(kAcquirePath, QByteArrayLiteral("0"));
}

// ── failAndStop ───────────────────────────────────────────────────────────────

void Mmb0DataSource::failAndStop(const QString& message) {
    emit errorOccurred(message);
    stop();
}

} // namespace studio::mmb0
