// src/core/acquisition/mmb0/Mmb0DataSource.cpp
#include "core/acquisition/mmb0/Mmb0DataSource.h"
#include "core/acquisition/mmb0/Styx9pClient.h"
#include "core/acquisition/SourceCapabilities.h"
#include <QTimer>

namespace studio::mmb0 {

// ── Static helpers ────────────────────────────────────────────────────────────

// Maps toRegisterBytes() array index → 9P conf node name.
// Returns empty string for indices that are not modelled (skip them).
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
    pollTimer_->setInterval(5);
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
    blocksizeSamples_ = n;
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

void Mmb0DataSource::pollOnce() {
    if (!running_) return;

    const uint32_t want = uint32_t(blocksizeSamples_ * 27);
    QByteArray buf;
    while (buf.size() < int(want)) {
        QByteArray chunk;
        if (!client_->readFid(dataFid_, 0, want - uint32_t(buf.size()), chunk)) {
            emit errorOccurred(QStringLiteral("Read from /data failed: ") + client_->lastError());
            stop();
            return;
        }
        if (chunk.isEmpty()) break;   // device had nothing more this poll
        buf.append(chunk);
    }

    if (!buf.isEmpty()) {
        parser_.feed(buf);
        QVector<studio::EegFrame> frames = parser_.takeFrames();
        if (!frames.empty()) {
            studio::EegFrameBatch batch(frames.begin(), frames.end());
            emit framesReady(batch);
        }
    }
}

// ── bringUp ──────────────────────────────────────────────────────────────────

bool Mmb0DataSource::bringUp() {
    // (Re)create the client so start() can be called repeatedly after stop().
    delete client_;
    client_ = new studio::styx::Styx9pClient(transport_, 3000);

    if (!client_->connectSession(8192, QStringLiteral("9P2000.USB.estyx"))) {
        emit errorOccurred(QStringLiteral("connectSession failed: ") + client_->lastError());
        return false;
    }

    if (!client_->attach(QString(), QString())) {
        emit errorOccurred(QStringLiteral("attach failed: ") + client_->lastError());
        return false;
    }

    // Write each modelled register byte
    const auto regs = config_.toRegisterBytes();
    for (int i = 0; i < int(regs.size()); ++i) {
        const QString name = regIndexToName(i);
        if (name.isEmpty()) continue;
        const QString path = QStringLiteral("/ads1299evm/conf/") + name;
        const QByteArray payload(1, char(regs[i]));
        if (!client_->writePath(path, payload)) {
            emit errorOccurred(QStringLiteral("Failed to write ") + path
                               + QStringLiteral(": ") + client_->lastError());
            return false;
        }
    }

    // Write blocksize
    {
        QByteArray bsPayload = QByteArray::number(blocksizeSamples_);
        if (!client_->writePath(QStringLiteral("/ads1299evm/blocksize_samples"), bsPayload)) {
            emit errorOccurred(QStringLiteral("Failed to write blocksize_samples: ")
                               + client_->lastError());
            return false;
        }
    }

    // Walk to and open /ads1299evm/data
    uint32_t iounit = 0;
    if (!client_->walkTo(QStringLiteral("/ads1299evm/data"), dataFid_)) {
        emit errorOccurred(QStringLiteral("walkTo /data failed: ") + client_->lastError());
        return false;
    }
    if (!client_->openFid(dataFid_, 0 /*OREAD*/, iounit)) {
        emit errorOccurred(QStringLiteral("openFid /data failed: ") + client_->lastError());
        return false;
    }

    // Start acquisition
    if (!client_->writePath(QStringLiteral("/ads1299evm/acquire"), QByteArray(1, '\x01'))) {
        emit errorOccurred(QStringLiteral("Failed to write acquire=1: ") + client_->lastError());
        return false;
    }

    return true;
}

// ── tearDown ──────────────────────────────────────────────────────────────────

void Mmb0DataSource::tearDown() {
    if (!client_) return;

    // Best-effort: write acquire=0
    client_->writePath(QStringLiteral("/ads1299evm/acquire"), QByteArray(1, '\x00'));

    // Clunk the data fid
    if (dataFid_ != 0) {
        client_->clunk(dataFid_);
        dataFid_ = 0;
    }
}

} // namespace studio::mmb0
