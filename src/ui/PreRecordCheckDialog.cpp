// src/ui/PreRecordCheckDialog.cpp
// See PreRecordCheckDialog.h for the public contract.

#include "ui/PreRecordCheckDialog.h"
#include "ui/theme/LedIndicator.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

namespace studio {

namespace {
constexpr int kNumChannels = 8;
} // namespace

// ── Static helpers ────────────────────────────────────────────────────────────

int PreRecordCheckDialog::okChannelCount(uint8_t leadOffP, uint8_t leadOffN)
{
    const uint8_t anyOff = leadOffP | leadOffN;
    int count = 0;
    for (int c = 0; c < kNumChannels; ++c) {
        if (!(anyOff & (1u << c))) {
            ++count;
        }
    }
    return count;
}

QString PreRecordCheckDialog::channelStatusText(uint8_t leadOffP, uint8_t leadOffN)
{
    const int ok = okChannelCount(leadOffP, leadOffN);
    const uint8_t anyOff = leadOffP | leadOffN;

    if (ok == kNumChannels) {
        return QString("%1 of %2 electrodes connected").arg(ok).arg(kNumChannels);
    }

    QStringList offNames;
    for (int c = 0; c < kNumChannels; ++c) {
        if (anyOff & (1u << c)) {
            offNames << QString("CH%1").arg(c + 1);
        }
    }
    return QString("%1 of %2 electrodes connected — %3 lead-off")
        .arg(ok)
        .arg(kNumChannels)
        .arg(offNames.join(", "));
}

// ── Dialog constructor ────────────────────────────────────────────────────────

PreRecordCheckDialog::PreRecordCheckDialog(uint8_t leadOffP, uint8_t leadOffN,
                                           QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Signal Quality Check");
    setModal(true);

    const int ok      = okChannelCount(leadOffP, leadOffN);
    const bool allOk  = (ok == kNumChannels);
    const uint8_t anyOff = leadOffP | leadOffN;

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // ── Header summary ──
    auto* headerLabel = new QLabel(channelStatusText(leadOffP, leadOffN), this);
    headerLabel->setObjectName("qualityHeader");
    headerLabel->setWordWrap(true);
    const QString headerColor = allOk ? "#3fb950" : "#d29922";
    headerLabel->setStyleSheet(
        QString("font-weight: bold; font-size: 13px; color: %1;").arg(headerColor));
    mainLayout->addWidget(headerLabel);

    if (!allOk) {
        auto* warnLabel = new QLabel(
            "Some electrodes are off — you can still proceed.", this);
        warnLabel->setObjectName("qualityWarn");
        warnLabel->setStyleSheet("color: #d29922;");
        mainLayout->addWidget(warnLabel);
    }

    // ── Per-channel grid ──
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);

    for (int c = 0; c < kNumChannels; ++c) {
        const bool off = (anyOff & (1u << c)) != 0;

        auto* chLabel = new QLabel(QString("CH%1").arg(c + 1), this);
        chLabel->setObjectName(QString("chLabel%1").arg(c + 1));

        auto* led = new LedIndicator(this);
        led->setObjectName(QString("led%1").arg(c + 1));
        led->setStatus(off ? LedIndicator::Status::Error : LedIndicator::Status::Ok);

        auto* statusLabel = new QLabel(off ? "LEAD-OFF" : "connected", this);
        statusLabel->setObjectName(QString("chStatus%1").arg(c + 1));
        if (off) {
            statusLabel->setStyleSheet("color: #f85149; font-weight: bold;");
        } else {
            statusLabel->setStyleSheet("color: #3fb950;");
        }

        grid->addWidget(chLabel,     c, 0);
        grid->addWidget(led,         c, 1);
        grid->addWidget(statusLabel, c, 2);
    }
    mainLayout->addLayout(grid);

    // ── Buttons ──
    auto* buttons = new QDialogButtonBox(this);
    auto* startBtn  = buttons->addButton("Start Recording", QDialogButtonBox::AcceptRole);
    auto* cancelBtn = buttons->addButton("Cancel",          QDialogButtonBox::RejectRole);
    startBtn->setObjectName("startRecordingBtn");
    cancelBtn->setObjectName("cancelBtn");

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(buttons);
}

} // namespace studio
