// src/ui/RegistersView.cpp
//
// See RegistersView.h for the public contract.

#include "ui/RegistersView.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "app/SessionController.h"
#include "core/device/DeviceConfig.h"
#include "ui/theme/Theme.h"

namespace studio {

// ---------------------------------------------------------------------------
// Helpers — combo item lists
// ---------------------------------------------------------------------------

static const QStringList kSampleRateItems = {
    "250 SPS", "500 SPS", "1000 SPS", "2000 SPS",
    "4000 SPS", "8000 SPS", "16000 SPS"
};
static const int kSampleRateValues[] = {
    250, 500, 1000, 2000, 4000, 8000, 16000
};
static const int kSampleRateCount = 7;

static const QStringList kGainItems = {
    "×1", "×2", "×4", "×6", "×8", "×12", "×24"
};
static const int kGainValues[] = { 1, 2, 4, 6, 8, 12, 24 };

static const QStringList kMuxItems = {
    "Normal", "Shorted", "BIAS_meas", "MVDD",
    "Temp", "Test", "BIAS_DRP", "BIAS_DRN"
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

RegistersView::RegistersView(SessionController* controller, QWidget* parent)
    : QWidget(parent)
    , controller_(controller)
{
    const DeviceConfig cfg = controller_->config();

    // ---- Top: sample rate row -----------------------------------------------
    auto* srLabel     = new QLabel("Sample Rate:", this);
    sampleRateCombo_  = new QComboBox(this);
    sampleRateCombo_->setObjectName("sampleRateCombo");
    for (const auto& item : kSampleRateItems)
        sampleRateCombo_->addItem(item);

    // Select the current sample rate
    for (int i = 0; i < kSampleRateCount; ++i) {
        if (kSampleRateValues[i] == cfg.sampleRate()) {
            sampleRateCombo_->setCurrentIndex(i);
            break;
        }
    }

    auto* srRow = new QHBoxLayout;
    srRow->addWidget(srLabel);
    srRow->addWidget(sampleRateCombo_);
    srRow->addStretch();

    // ---- Per-channel grid ---------------------------------------------------
    auto* chanGroup = new QGroupBox("Channels", this);
    auto* chanGrid  = new QGridLayout(chanGroup);
    chanGrid->setSpacing(4);

    // Header row
    chanGrid->addWidget(new QLabel("CH"),   0, 0);
    chanGrid->addWidget(new QLabel("Gain"), 0, 1);
    chanGrid->addWidget(new QLabel("Mux"),  0, 2);

    const auto gains = cfg.gain();
    const auto muxes = cfg.mux();

    for (int ch = 0; ch < 8; ++ch) {
        auto* chLabel = new QLabel(QString("CH%1").arg(ch + 1), this);

        gainCombo_[ch] = new QComboBox(this);
        gainCombo_[ch]->setObjectName(QString("gainCombo%1").arg(ch));
        for (const auto& item : kGainItems)
            gainCombo_[ch]->addItem(item);

        // Select current gain
        for (int gi = 0; gi < static_cast<int>(std::size(kGainValues)); ++gi) {
            if (kGainValues[gi] == gains[static_cast<std::size_t>(ch)]) {
                gainCombo_[ch]->setCurrentIndex(gi);
                break;
            }
        }

        muxCombo_[ch] = new QComboBox(this);
        muxCombo_[ch]->setObjectName(QString("muxCombo%1").arg(ch));
        for (const auto& item : kMuxItems)
            muxCombo_[ch]->addItem(item);
        muxCombo_[ch]->setCurrentIndex(muxes[static_cast<std::size_t>(ch)]);

        chanGrid->addWidget(chLabel,        ch + 1, 0);
        chanGrid->addWidget(gainCombo_[ch], ch + 1, 1);
        chanGrid->addWidget(muxCombo_[ch],  ch + 1, 2);
    }

    // ---- Reference / BIAS row -----------------------------------------------
    srb1Check_ = new QCheckBox("SRB1 reference", this);
    srb1Check_->setObjectName("srb1Check");
    srb1Check_->setChecked(cfg.srb1());

    biasEnabledCheck_ = new QCheckBox("Bias drive", this);
    biasEnabledCheck_->setObjectName("biasEnabledCheck");
    biasEnabledCheck_->setChecked(cfg.biasEnabled());

    auto* refRow = new QHBoxLayout;
    refRow->addWidget(srb1Check_);
    refRow->addWidget(biasEnabledCheck_);
    refRow->addStretch();

    // ---- Hex preview --------------------------------------------------------
    hexPreview_ = new QPlainTextEdit(this);
    hexPreview_->setObjectName("hexPreview");
    hexPreview_->setReadOnly(true);
    hexPreview_->setMaximumHeight(80);

    QFont mono;
    mono.setFamily(QString(theme::kMonoFamily).split(',').first().remove('"').trimmed());
    mono.setPointSize(10);
    hexPreview_->setFont(mono);
    hexPreview_->setStyleSheet(
        QString("QPlainTextEdit { background: %1; color: %2; border: 1px solid %3; "
                "font-family: %4; font-size: 10pt; }")
            .arg(theme::kSurface)
            .arg(theme::kTextPrimary)
            .arg(theme::kBorder)
            .arg(theme::kMonoFamily)
    );

    // ---- Apply button -------------------------------------------------------
    applyButton_ = new QPushButton("Apply", this);
    applyButton_->setObjectName("primary");

    // ---- Main layout --------------------------------------------------------
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(theme::spacing(1));
    mainLayout->setContentsMargins(
        theme::spacing(1), theme::spacing(1),
        theme::spacing(1), theme::spacing(1)
    );
    mainLayout->addLayout(srRow);
    mainLayout->addWidget(chanGroup);
    mainLayout->addLayout(refRow);
    mainLayout->addWidget(new QLabel("Register bytes (live preview):", this));
    mainLayout->addWidget(hexPreview_);
    mainLayout->addWidget(applyButton_);
    mainLayout->addStretch();

    // ---- Wire signals -------------------------------------------------------
    connect(sampleRateCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RegistersView::onAnyControlChanged);

    for (int ch = 0; ch < 8; ++ch) {
        connect(gainCombo_[ch], QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &RegistersView::onAnyControlChanged);
        connect(muxCombo_[ch], QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &RegistersView::onAnyControlChanged);
    }

    connect(srb1Check_,        &QCheckBox::toggled, this, &RegistersView::onAnyControlChanged);
    connect(biasEnabledCheck_, &QCheckBox::toggled, this, &RegistersView::onAnyControlChanged);
    connect(applyButton_,      &QPushButton::clicked, this, &RegistersView::onApplyClicked);

    // Initial preview
    refreshPreview();
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void RegistersView::onAnyControlChanged()
{
    refreshPreview();
}

void RegistersView::onApplyClicked()
{
    controller_->applyConfig(currentConfig());
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

DeviceConfig RegistersView::currentConfig() const
{
    // Read sample rate
    const int srIdx = sampleRateCombo_->currentIndex();
    const int sps   = (srIdx >= 0 && srIdx < kSampleRateCount)
                    ? kSampleRateValues[srIdx] : 250;

    DeviceConfig cfg = DeviceConfig().withSampleRate(sps)
                                     .withSrb1(srb1Check_->isChecked())
                                     .withBiasEnabled(biasEnabledCheck_->isChecked());

    for (int ch = 0; ch < 8; ++ch) {
        const int gi      = gainCombo_[ch]->currentIndex();
        const int gainVal = (gi >= 0 && gi < static_cast<int>(std::size(kGainValues)))
                          ? kGainValues[gi] : 24;
        const int muxVal  = muxCombo_[ch]->currentIndex();

        cfg = cfg.withGain(ch, gainVal).withMux(ch, muxVal);
    }

    return cfg;
}

void RegistersView::refreshPreview()
{
    const DeviceConfig cfg  = currentConfig();
    const QString dump = registerHexDump(cfg.toRegisterBytes());
    // Format as wrapped lines (one register per line if too wide)
    // Split into pairs for readability: 4 per line
    const QStringList tokens = dump.split(' ');
    QStringList lines;
    for (int i = 0; i < tokens.size(); i += 4) {
        lines.append(tokens.mid(i, 4).join("  "));
    }
    hexPreview_->setPlainText(lines.join('\n'));
}

} // namespace studio
