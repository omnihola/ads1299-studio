// src/ui/AcquisitionPanel.cpp
//
// See AcquisitionPanel.h for public contract.

#include "ui/AcquisitionPanel.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <cmath>

#include "app/SessionController.h"
#include "ui/theme/Theme.h"

namespace studio {

// ──────────────────────────────────────────────────────────────────────────────
// Pure static helpers
// ──────────────────────────────────────────────────────────────────────────────

double AcquisitionPanel::resolutionMicrovolts(int gain)
{
    // Vref = 4.5 V, ADC 24-bit (2^23 counts for full scale)
    return (4.5 / (gain * 8388608.0)) * 1e6;
}

double AcquisitionPanel::inputRangeMillivolts(int gain)
{
    return (4.5 / gain) * 1e3;
}

double AcquisitionPanel::nyquistHz(int sps)
{
    return sps / 2.0;
}

// ──────────────────────────────────────────────────────────────────────────────
// Construction
// ──────────────────────────────────────────────────────────────────────────────

AcquisitionPanel::AcquisitionPanel(SessionController* controller, QWidget* parent)
    : QWidget(parent)
    , controller_(controller)
{
    buildUi();

    // Initial state from controller config
    const DeviceConfig cfg = controller_->config();
    {
        QSignalBlocker b1(spsCombo_);
        QSignalBlocker b2(gainCombo_);
        spsCombo_->setCurrentText(QString("%1 Hz").arg(cfg.sampleRate()));
        gainCombo_->setCurrentText(QString("%1×").arg(cfg.gain()[0]));
    }
    refreshReadouts(cfg.gain()[0], cfg.sampleRate());

    // React to changes made elsewhere (e.g., Registers tab)
    connect(controller_, &SessionController::configChanged,
            this, &AcquisitionPanel::onConfigChanged);

    // Disable controls while recording to prevent mid-recording config changes.
    connect(controller_, &SessionController::recordingChanged,
            this, &AcquisitionPanel::onRecordingChanged);
}

// ──────────────────────────────────────────────────────────────────────────────
// UI construction
// ──────────────────────────────────────────────────────────────────────────────

void AcquisitionPanel::buildUi()
{
    const QString bgColor      = theme::kSurface;
    const QString borderColor  = theme::kBorder;
    const QString textPrimary  = theme::kTextPrimary;
    const QString textMuted    = theme::kTextMuted;
    const QString monoFamily   = theme::kMonoFamily;
    const QString accent       = theme::kAccent;

    setMinimumWidth(200);
    setObjectName("acquisitionPanel");
    setStyleSheet(QString(
        "QWidget#acquisitionPanel { background: %1; }"
        "QLabel { color: %2; font-size: 12px; }"
        "QComboBox { background: %3; color: %2; border: 1px solid %4;"
        "  border-radius: 4px; padding: 3px 6px; font-size: 12px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: %3; color: %2; border: 1px solid %4; }")
        .arg(bgColor, textPrimary, bgColor, borderColor));

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(8, 10, 8, 10);
    outerLayout->setSpacing(12);

    // ── Controls group ───────────────────────────────────────────────────────
    controlsBox_ = new QGroupBox("Controls", this);
    auto* controlsBox = controlsBox_;
    controlsBox->setStyleSheet(QString(
        "QGroupBox { color: %1; border: 1px solid %2; border-radius: 4px;"
        "  margin-top: 6px; font-size: 11px; font-weight: bold; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px;"
        "  padding: 0 4px; color: %3; }")
        .arg(textMuted, borderColor, accent));

    auto* formLayout = new QFormLayout(controlsBox);
    formLayout->setContentsMargins(8, 14, 8, 8);
    formLayout->setSpacing(8);
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Sample rate combo
    spsCombo_ = new QComboBox(this);
    spsCombo_->setObjectName("spsCombo");
    for (int sps : {250, 500, 1000, 2000, 4000, 8000, 16000})
        spsCombo_->addItem(QString("%1 Hz").arg(sps), sps);

    auto* spsLabel = new QLabel("Sample rate:", this);
    spsLabel->setStyleSheet(QString("color: %1;").arg(textMuted));
    formLayout->addRow(spsLabel, spsCombo_);

    // Gain combo
    gainCombo_ = new QComboBox(this);
    gainCombo_->setObjectName("gainCombo");
    for (int g : {1, 2, 4, 6, 8, 12, 24})
        gainCombo_->addItem(QString("%1×").arg(g), g);

    auto* gainLabel = new QLabel("Gain (all ch):", this);
    gainLabel->setStyleSheet(QString("color: %1;").arg(textMuted));
    formLayout->addRow(gainLabel, gainCombo_);

    outerLayout->addWidget(controlsBox);

    // ── Live readouts group ──────────────────────────────────────────────────
    auto* readoutsBox = new QGroupBox("Live Readouts", this);
    readoutsBox->setStyleSheet(controlsBox->styleSheet());

    auto* readoutsLayout = new QVBoxLayout(readoutsBox);
    readoutsLayout->setContentsMargins(8, 14, 8, 8);
    readoutsLayout->setSpacing(6);

    const QString monoStyle = QString(
        "color: %1; font-family: %2; font-size: 11px;")
        .arg(textPrimary, monoFamily);

    resolutionLabel_ = new QLabel(this);
    resolutionLabel_->setObjectName("resolutionLabel");
    resolutionLabel_->setStyleSheet(monoStyle);

    inputRangeLabel_ = new QLabel(this);
    inputRangeLabel_->setObjectName("inputRangeLabel");
    inputRangeLabel_->setStyleSheet(monoStyle);

    bandwidthLabel_ = new QLabel(this);
    bandwidthLabel_->setObjectName("bandwidthLabel");
    bandwidthLabel_->setStyleSheet(monoStyle);

    readoutsLayout->addWidget(resolutionLabel_);
    readoutsLayout->addWidget(inputRangeLabel_);
    readoutsLayout->addWidget(bandwidthLabel_);

    outerLayout->addWidget(readoutsBox);

    // ── Help note ────────────────────────────────────────────────────────────
    auto* hintLabel = new QLabel(
        "Changes apply live.\nPer-channel gain/mux is in the Registers tab.", this);
    hintLabel->setObjectName("hintLabel");
    hintLabel->setWordWrap(true);
    hintLabel->setStyleSheet(QString(
        "color: %1; font-size: 11px; font-style: italic;").arg(textMuted));
    outerLayout->addWidget(hintLabel);

    outerLayout->addStretch();

    // Connect combos
    connect(spsCombo_,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AcquisitionPanel::onComboChanged);
    connect(gainCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AcquisitionPanel::onComboChanged);
}

// ──────────────────────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────────────────────

void AcquisitionPanel::refreshReadouts(int gain, int sps)
{
    const double resUv  = resolutionMicrovolts(gain);
    const double rangeMv = inputRangeMillivolts(gain);
    const double bwHz   = nyquistHz(sps);

    resolutionLabel_->setText(
        QString("Resolution:  %1 µV / LSB").arg(resUv, 0, 'f', 4));
    inputRangeLabel_->setText(
        QString("Input range: ±%1 mV").arg(rangeMv, 0, 'f', 1));
    bandwidthLabel_->setText(
        QString("Bandwidth:   %1 Hz (Nyquist)").arg(bwHz, 0, 'f', 0));
}

// ──────────────────────────────────────────────────────────────────────────────
// Slots
// ──────────────────────────────────────────────────────────────────────────────

void AcquisitionPanel::onComboChanged()
{
    const int sps  = spsCombo_->currentData().toInt();
    const int gain = gainCombo_->currentData().toInt();

    // Build new immutable config: apply SPS, then set chosen gain on all 8 channels
    DeviceConfig cfg = controller_->config().withSampleRate(sps);
    for (int c = 0; c < 8; ++c)
        cfg = cfg.withGain(c, gain);

    controller_->applyConfig(cfg);
    refreshReadouts(gain, sps);
}

void AcquisitionPanel::onConfigChanged(studio::DeviceConfig cfg)
{
    // Keep combos in sync with external config changes (e.g., Registers tab).
    // Block signals to avoid a feedback loop back into applyConfig.
    {
        QSignalBlocker b1(spsCombo_);
        QSignalBlocker b2(gainCombo_);

        spsCombo_->setCurrentText(QString("%1 Hz").arg(cfg.sampleRate()));
        gainCombo_->setCurrentText(QString("%1×").arg(cfg.gain()[0]));
    }
    refreshReadouts(cfg.gain()[0], cfg.sampleRate());
}

// ──────────────────────────────────────────────────────────────────────────────
// Test hook
// ──────────────────────────────────────────────────────────────────────────────

void AcquisitionPanel::setSampleRateSelection(int sps)
{
    spsCombo_->setCurrentText(QString("%1 Hz").arg(sps));
}

// ──────────────────────────────────────────────────────────────────────────────
// Recording-state lock
// ──────────────────────────────────────────────────────────────────────────────

void AcquisitionPanel::onRecordingChanged(bool recording)
{
    // Disable the Controls group while recording so sample-rate and gain
    // cannot be changed mid-session (which would desync the BDF time-base
    // and µV scaling fixed at Recorder::open()). Live readouts stay visible.
    controlsBox_->setEnabled(!recording);
    if (recording) {
        controlsBox_->setToolTip("Locked during recording");
    } else {
        controlsBox_->setToolTip({});
    }
}

} // namespace studio
