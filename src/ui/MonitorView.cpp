// src/ui/MonitorView.cpp
//
// See MonitorView.h for public contract.

#include "ui/MonitorView.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <limits>

#include "qcustomplot.h"

#include "app/SessionController.h"
#include "core/Constants.h"
#include "core/acquisition/EegFrame.h"
#include "core/dsp/Decimate.h"
#include "core/dsp/ScaleConverter.h"
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
    stylePlot();
    setupGraphs();

    // Wire render timer
    connect(&renderTimer_, &QTimer::timeout, this, &MonitorView::onRenderTick);
    renderTimer_.setInterval(kRenderIntervalMs);
    renderTimer_.start();
}

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────

void MonitorView::clear()
{
    for (int c = 0; c < kNumChannels; ++c) {
        channelSamples_[c].clear();
    }
    timeSamples_.clear();
    currentTimeSec_ = 0.0;

    for (int c = 0; c < kNumChannels; ++c) {
        graphs_[c]->data()->clear();
    }
    plot_->replot(QCustomPlot::rpQueuedReplot);
}

void MonitorView::setSampleRate(int hz)
{
    if (hz > 0) {
        sampleRateHz_ = hz;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Render tick
// ──────────────────────────────────────────────────────────────────────────────

void MonitorView::onRenderTick()
{
    if (paused_ || controller_ == nullptr) {
        return;
    }

    const double dt = 1.0 / static_cast<double>(sampleRateHz_);
    ScaleConverter converter(gain_);

    EegFrame frame;
    bool gotAny = false;

    while (controller_->displayBuffer().pop(frame)) {
        gotAny = true;
        timeSamples_.append(currentTimeSec_);
        currentTimeSec_ += dt;

        for (int c = 0; c < kNumChannels; ++c) {
            const double uv = converter.countsToMicrovolts(frame.ch[c]);
            channelSamples_[c].append(uv + offsetUv(c));
        }
    }

    if (!gotAny) {
        return;
    }

    // Trim samples outside the time window
    const double windowStart = currentTimeSec_ - kWindowSeconds;
    while (!timeSamples_.isEmpty() && timeSamples_.first() < windowStart) {
        timeSamples_.removeFirst();
        for (int c = 0; c < kNumChannels; ++c) {
            channelSamples_[c].removeFirst();
        }
    }

    // Update x-axis range
    const double xMin = timeSamples_.isEmpty() ? 0.0 : timeSamples_.first();
    const double xMax = xMin + kWindowSeconds;
    plot_->xAxis->setRange(xMin, xMax);

    // Update each channel graph using decimated data
    const int plotWidthPx = std::max(1, plot_->width());
    const int maxPoints   = plotWidthPx * 2;

    for (int c = 0; c < kNumChannels; ++c) {
        const QVector<double>& tVec = timeSamples_;
        const QVector<double>& yVec = channelSamples_[c];

        if (tVec.isEmpty()) {
            graphs_[c]->data()->clear();
            continue;
        }

        // Decimate y values; apply same decimation index selection to t
        const int n = tVec.size();
        if (n <= maxPoints) {
            // No decimation needed
            graphs_[c]->setData(tVec, yVec, /*alreadySorted=*/true);
        } else {
            // Decimate via min/max envelope; rebuild corresponding t vector
            const QVector<double> decimatedY = decimateMinMax(yVec, maxPoints);

            // Reconstruct matching t values by computing bucket indices
            const int bucketCount = std::max(1, maxPoints / 2);
            QVector<double> decimatedT;
            decimatedT.reserve(decimatedY.size());

            for (int b = 0; b < bucketCount; ++b) {
                const int start = (b * n) / bucketCount;
                const int end   = ((b + 1) * n) / bucketCount;
                if (start >= end) continue;

                // Find min/max indices within slice (mirrors Decimate.cpp logic)
                int    minIdx = start;
                int    maxIdx = start;
                double minVal = yVec[start];
                double maxVal = yVec[start];

                for (int i = start + 1; i < end; ++i) {
                    const double v = yVec[i];
                    if (v < minVal) { minVal = v; minIdx = i; }
                    if (v > maxVal) { maxVal = v; maxIdx = i; }
                }

                if (minIdx <= maxIdx) {
                    decimatedT.append(tVec[minIdx]);
                    decimatedT.append(tVec[maxIdx]);
                } else {
                    decimatedT.append(tVec[maxIdx]);
                    decimatedT.append(tVec[minIdx]);
                }
            }

            // Trim to same length in case of rounding discrepancies
            const int outSize = std::min(decimatedT.size(), decimatedY.size());
            graphs_[c]->setData(decimatedT.mid(0, outSize),
                                decimatedY.mid(0, outSize),
                                /*alreadySorted=*/true);
        }
    }

    plot_->replot(QCustomPlot::rpQueuedReplot);
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
        applyYScale();
    });

    controlLayout->addWidget(pauseButton_);
    controlLayout->addStretch();
    controlLayout->addWidget(uvLabel);
    controlLayout->addWidget(uvDivCombo_);

    // ---- Plot ----------------------------------------------------------
    plot_ = new QCustomPlot(this);
    plot_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    mainLayout->addWidget(controlBar, 0);
    mainLayout->addWidget(plot_, 1);
}

void MonitorView::stylePlot()
{
    // Background
    plot_->setBackground(QColor(theme::kBg));
    plot_->axisRect()->setBackground(QColor(theme::kBg));

    // Axes pens
    const QColor borderColor = QColor(theme::kBorder);
    const QPen   borderPen   = QPen(borderColor);
    plot_->xAxis->setBasePen(borderPen);
    plot_->yAxis->setBasePen(borderPen);
    plot_->xAxis->setTickPen(borderPen);
    plot_->yAxis->setTickPen(borderPen);
    plot_->xAxis->setSubTickPen(borderPen);
    plot_->yAxis->setSubTickPen(borderPen);

    // Tick label colors
    const QColor mutedColor = QColor(theme::kTextMuted);
    plot_->xAxis->setTickLabelColor(mutedColor);
    plot_->yAxis->setTickLabelColor(mutedColor);
    plot_->xAxis->setLabelColor(mutedColor);
    plot_->yAxis->setLabelColor(mutedColor);

    // Grid
    const QPen gridPen = QPen(borderColor, 1, Qt::DotLine);
    plot_->xAxis->grid()->setPen(gridPen);
    plot_->yAxis->grid()->setPen(gridPen);
    plot_->xAxis->grid()->setZeroLinePen(Qt::NoPen);
    plot_->yAxis->grid()->setZeroLinePen(Qt::NoPen);

    // Axis labels
    plot_->xAxis->setLabel("Time (s)");
    plot_->xAxis->setRange(0, kWindowSeconds);

    // Hide legend
    plot_->legend->setVisible(false);

    // No user interactions
    plot_->setInteractions(QCP::Interactions());

    // Y-axis channel labels via custom ticker
    auto ticker = QSharedPointer<QCPAxisTickerText>::create();
    for (int c = 0; c < kNumChannels; ++c) {
        ticker->addTick(offsetUv(c), QString("CH%1").arg(c + 1));
    }
    plot_->yAxis->setTicker(ticker);
    plot_->yAxis->setTickLabelColor(mutedColor);

    applyYScale();
}

void MonitorView::setupGraphs()
{
    const QPen signalPen = QPen(QColor(theme::kSignal), 1);

    for (int c = 0; c < kNumChannels; ++c) {
        graphs_[c] = plot_->addGraph();
        graphs_[c]->setPen(signalPen);
        graphs_[c]->setLineStyle(QCPGraph::lsLine);
        graphs_[c]->setScatterStyle(QCPScatterStyle::ssNone);
    }
}

double MonitorView::offsetUv(int channel) const
{
    return static_cast<double>(kNumChannels - 1 - channel) * kChannelSpacingUv;
}

void MonitorView::applyYScale()
{
    // y range spans all 8 channel baselines ± half a div on each side
    const double halfSpan  = kChannelSpacingUv * (kNumChannels - 1) / 2.0 + uvPerDiv_;
    const double midOffset = kChannelSpacingUv * (kNumChannels - 1) / 2.0;
    plot_->yAxis->setRange(midOffset - halfSpan, midOffset + halfSpan);

    if (plot_->isVisible()) {
        plot_->replot(QCustomPlot::rpQueuedReplot);
    }
}

} // namespace studio
