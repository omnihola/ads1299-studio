// src/ui/MonitorView.cpp
//
// See MonitorView.h for public contract.

#include "ui/MonitorView.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPair>
#include <QPushButton>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

#include "app/SessionController.h"
#include "core/Constants.h"
#include "core/acquisition/EegFrame.h"
#include "core/dsp/ScaleConverter.h"
#include "ui/gl/GlWaveformWidget.h"
#include "ui/theme/Theme.h"

namespace studio {

// ──────────────────────────────────────────────────────────────────────────────
// Construction
// ──────────────────────────────────────────────────────────────────────────────

MonitorView::MonitorView(SessionController* controller, QWidget* parent)
    : QWidget(parent)
    , controller_(controller)
{
    buildLayout();

    // Initialise per-channel gains and sample rate from the current device config.
    if (controller_) {
        const studio::DeviceConfig initialCfg = controller_->config();
        const auto gainArr = initialCfg.gain();
        for (int c = 0; c < kNumChannels; ++c) {
            gains_[c] = gainArr[static_cast<size_t>(c)];
        }
        sampleRateHz_ = initialCfg.sampleRate();

        // Refresh gains and sample rate whenever the device config changes.
        connect(controller_, &SessionController::configChanged,
                this, [this](const studio::DeviceConfig& cfg) {
            const auto arr = cfg.gain();
            for (int c = 0; c < kNumChannels; ++c) {
                gains_[c] = arr[static_cast<size_t>(c)];
            }
            setSampleRate(cfg.sampleRate());
            rebuildFilterChain();
            clear();
        });
    }

    // Wire render timer
    connect(&renderTimer_, &QTimer::timeout, this, &MonitorView::onRenderTick);
    renderTimer_.setInterval(kRenderIntervalMs);
    renderTimer_.start();

    // Build initial display filter chain (Off/Off by default — passthrough).
    rebuildFilterChain();
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────

void MonitorView::clear()
{
    if (glPlot_) {
        glPlot_->clearData();
    }
    filterChain_.reset();
}

void MonitorView::setSampleRate(int hz)
{
    if (hz > 0) {
        sampleRateHz_ = hz;
        if (glPlot_) {
            glPlot_->setSampleRate(static_cast<double>(hz));
        }
    }
}

void MonitorView::togglePause()
{
    // Flip the paused_ state via the button so its visual check state stays in sync.
    pauseButton_->setChecked(!paused_);
}

// ──────────────────────────────────────────────────────────────────────────────
// Render tick
// ──────────────────────────────────────────────────────────────────────────────

void MonitorView::onRenderTick()
{
    if (paused_ || controller_ == nullptr) {
        return;
    }

    EegFrame frame;
    bool gotAny = false;

    // Accumulate samples across all popped frames into per-channel blocks.
    std::vector<std::vector<float>> block(static_cast<size_t>(kNumChannels));

    while (controller_->displayBuffer().pop(frame)) {
        gotAny = true;
        for (int c = 0; c < kNumChannels; ++c) {
            // Use per-channel configured gain so µV scale is correct.
            ScaleConverter converter(gains_[c]);
            const double uv = converter.countsToMicrovolts(frame.ch[c]);
            // Apply display-only filter chain (notch/bandpass if configured).
            // The raw frame is NOT modified — recording path is entirely unaffected.
            const double displayUv = filterChain_.process(c, uv);
            block[static_cast<size_t>(c)].push_back(static_cast<float>(displayUv));
        }
    }

    if (!gotAny) {
        return;
    }

    if (glPlot_) {
        glPlot_->pushBlock(block);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Private builders
// ──────────────────────────────────────────────────────────────────────────────

void MonitorView::buildLayout()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ---- Top control bar -----------------------------------------------
    auto* controlBar    = new QWidget(this);
    auto* controlLayout = new QHBoxLayout(controlBar);
    controlLayout->setContentsMargins(8, 4, 8, 4);
    controlLayout->setSpacing(8);

    pauseButton_ = new QPushButton("Pause", controlBar);
    pauseButton_->setCheckable(true);
    pauseButton_->setFixedWidth(64);
    connect(pauseButton_, &QPushButton::toggled, this, [this](bool checked) {
        paused_ = checked;
        pauseButton_->setText(checked ? "Resume" : "Pause");
        if (glPlot_) {
            glPlot_->setPaused(checked);
        }
    });

    auto* uvLabel = new QLabel("µV/div:", controlBar);
    uvLabel->setStyleSheet(QString("color: %1;").arg(theme::kTextMuted));

    uvDivCombo_ = new QComboBox(controlBar);
    const QList<int> uvDivValues = {50, 100, 200, 500, 1000};
    for (int v : uvDivValues) {
        uvDivCombo_->addItem(QString::number(v), v);
    }
    uvDivCombo_->setCurrentIndex(2); // default 200
    connect(uvDivCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int /*idx*/) {
        uvPerDiv_ = uvDivCombo_->currentData().toDouble();
        if (glPlot_) {
            glPlot_->setMicrovoltsPerDiv(uvPerDiv_);
        }
    });

    // ---- Filter controls -----------------------------------------------
    auto* notchLabel = new QLabel("Notch:", controlBar);
    notchLabel->setStyleSheet(QString("color: %1;").arg(theme::kTextMuted));

    notchCombo_ = new QComboBox(controlBar);
    notchCombo_->addItem("Off",   0.0);
    notchCombo_->addItem("50 Hz", 50.0);
    notchCombo_->addItem("60 Hz", 60.0);
    connect(notchCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MonitorView::onFilterChanged);

    auto* bpLabel = new QLabel("Band-pass:", controlBar);
    bpLabel->setStyleSheet(QString("color: %1;").arg(theme::kTextMuted));

    bpCombo_ = new QComboBox(controlBar);
    bpCombo_->addItem("Off",           QVariant::fromValue(QPair<double,double>(0.0, 0.0)));
    bpCombo_->addItem("0.5–40 Hz",     QVariant::fromValue(QPair<double,double>(0.5, 40.0)));
    bpCombo_->addItem("1–100 Hz",      QVariant::fromValue(QPair<double,double>(1.0, 100.0)));
    bpCombo_->addItem("8–13 Hz (α)",   QVariant::fromValue(QPair<double,double>(8.0, 13.0)));
    bpCombo_->addItem("13–30 Hz (β)",  QVariant::fromValue(QPair<double,double>(13.0, 30.0)));
    connect(bpCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MonitorView::onFilterChanged);

    filterHint_ = new QLabel("Filters affect display only — recording stays raw.", controlBar);
    filterHint_->setStyleSheet(QString("color: %1; font-style: italic; font-size: 10px;")
                               .arg(theme::kTextMuted));

    controlLayout->addWidget(pauseButton_);
    controlLayout->addStretch();
    controlLayout->addWidget(notchLabel);
    controlLayout->addWidget(notchCombo_);
    controlLayout->addWidget(bpLabel);
    controlLayout->addWidget(bpCombo_);
    controlLayout->addSpacing(12);
    controlLayout->addWidget(filterHint_);
    controlLayout->addStretch();
    controlLayout->addWidget(uvLabel);
    controlLayout->addWidget(uvDivCombo_);

    // ---- GL waveform plot ----------------------------------------------
    glPlot_ = new studio::gl::GlWaveformWidget(this);
    glPlot_->setChannelCount(kNumChannels);
    glPlot_->setSampleRate(static_cast<double>(sampleRateHz_));
    glPlot_->setWindowSeconds(5.0);
    glPlot_->setMicrovoltsPerDiv(uvPerDiv_);
    glPlot_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    mainLayout->addWidget(controlBar, 0);
    mainLayout->addWidget(glPlot_, 1);
}

// ──────────────────────────────────────────────────────────────────────────────
// Display filter chain
// ──────────────────────────────────────────────────────────────────────────────

void MonitorView::onFilterChanged()
{
    rebuildFilterChain();
    filterChain_.reset();
}

void MonitorView::rebuildFilterChain()
{
    // Notch
    double notchHz = 0.0;
    if (notchCombo_) {
        notchHz = notchCombo_->currentData().toDouble();
    }

    // Bandpass
    double bpLo = 0.0;
    double bpHi = 0.0;
    if (bpCombo_) {
        const QVariant v = bpCombo_->currentData();
        if (v.isValid()) {
            const auto pair = v.value<QPair<double,double>>();
            bpLo = pair.first;
            bpHi = pair.second;
        }
    }

    filterChain_.configure(kNumChannels,
                           static_cast<double>(sampleRateHz_),
                           notchHz, bpLo, bpHi);
}

} // namespace studio
