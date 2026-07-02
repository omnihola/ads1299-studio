// src/ui/SpectrumView.cpp
#include "ui/SpectrumView.h"

#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>

#include "qcustomplot.h"

#include "app/SessionController.h"
#include "core/dsp/FftProcessor.h"
#include "core/dsp/ScaleConverter.h"
#include "ui/theme/Theme.h"

namespace studio {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SpectrumView::SpectrumView(SessionController* controller, QWidget* parent)
    : QWidget(parent)
    , controller_(controller)
    , fft_(std::make_unique<FftProcessor>(kNfft))
{
    buildUi();
    applyTheme();

    timer_ = new QTimer(this);
    timer_->setInterval(200);
    connect(timer_, &QTimer::timeout, this, &SpectrumView::onTimer);
    timer_->start();
}

SpectrumView::~SpectrumView() = default;

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void SpectrumView::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // ---- Toolbar row ----
    auto* toolbar = new QHBoxLayout();
    toolbar->setSpacing(8);

    auto* chLabel = new QLabel("Channel:", this);
    channelBox_   = new QComboBox(this);
    for (int c = 1; c <= 8; ++c) {
        channelBox_->addItem(QString("CH%1").arg(c));
    }

    dbToggle_ = new QCheckBox("dB scale", this);
    dbToggle_->setChecked(true);

    toolbar->addWidget(chLabel);
    toolbar->addWidget(channelBox_);
    toolbar->addSpacing(12);
    toolbar->addWidget(dbToggle_);
    toolbar->addStretch();

    root->addLayout(toolbar);

    // ---- QCustomPlot ----
    plot_ = new QCustomPlot(this);
    plot_->addGraph();
    plot_->xAxis->setLabel("Frequency (Hz)");
    plot_->yAxis->setLabel("Power (dB)");
    plot_->setMinimumHeight(200);
    root->addWidget(plot_, /*stretch=*/1);
}

void SpectrumView::applyTheme()
{
    using namespace studio::theme;

    // Widget background
    setStyleSheet(QString("background:%1;").arg(kBg));

    // Plot background
    plot_->setBackground(QColor(kBg));
    plot_->xAxis->setBasePen(QPen(QColor(kBorder)));
    plot_->yAxis->setBasePen(QPen(QColor(kBorder)));
    plot_->xAxis->setTickPen(QPen(QColor(kBorder)));
    plot_->yAxis->setTickPen(QPen(QColor(kBorder)));
    plot_->xAxis->setSubTickPen(QPen(QColor(kBorder)));
    plot_->yAxis->setSubTickPen(QPen(QColor(kBorder)));
    plot_->xAxis->setTickLabelColor(QColor(kTextMuted));
    plot_->yAxis->setTickLabelColor(QColor(kTextMuted));
    plot_->xAxis->setLabelColor(QColor(kTextMuted));
    plot_->yAxis->setLabelColor(QColor(kTextMuted));

    // Graph line
    plot_->graph(0)->setPen(QPen(QColor(kSignal), 1.2));

    // Axis rect
    plot_->axisRect()->setBackground(QColor(kSurface));
}

// ---------------------------------------------------------------------------
// Timer slot — fetch samples, run FFT, update plot
// ---------------------------------------------------------------------------

void SpectrumView::onTimer()
{
    if (!controller_) return;

    const int channel = channelBox_->currentIndex();  // 0-based physical channel
    if (channel < 0 || channel >= 8) {
        return;
    }

    const QVector<double> samples =
        controller_->recentSamples(channel, kNfft);

    if (samples.size() < kNfft) {
        // Not enough data yet — nothing to plot
        return;
    }

    const DeviceConfig cfg = controller_->config();
    const auto gainArr = cfg.gain();
    ScaleConverter converter(gainArr[static_cast<size_t>(channel)]);

    std::vector<double> buf;
    buf.reserve(static_cast<size_t>(samples.size()));
    for (double sample : samples) {
        buf.push_back(converter.countsToMicrovolts(static_cast<int32_t>(sample)));
    }

    const std::vector<double> power = fft_->powerSpectrum(buf);

    const double fs = static_cast<double>(controller_->config().sampleRate());
    const int    nBins = fft_->numBins();

    QVector<double> xData(nBins), yData(nBins);
    const bool logScale = dbToggle_->isChecked();

    for (int k = 0; k < nBins; ++k) {
        xData[k] = fft_->binHz(k, fs);
        if (logScale) {
            // 10*log10(power + epsilon) to avoid log(0)
            yData[k] = 10.0 * std::log10(power[static_cast<size_t>(k)] + 1e-12);
        } else {
            yData[k] = power[static_cast<size_t>(k)];
        }
    }

    // Update x-axis label depending on mode
    plot_->yAxis->setLabel(logScale ? "Power (dB µV²)" : "Power (µV²)");

    plot_->graph(0)->setData(xData, yData);
    plot_->xAxis->setRange(0.0, fs / 2.0);
    plot_->yAxis->rescale(true);
    plot_->replot(QCustomPlot::rpQueuedReplot);
}

} // namespace studio
