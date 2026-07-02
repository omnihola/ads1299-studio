// src/ui/gl/GlWaveformWidget.cpp
//
// See GlWaveformWidget.h for the public contract.

#include "ui/gl/GlWaveformWidget.h"
#include "ui/gl/WaveformTransform.h"
#include "ui/gl/AutoScale.h"
#include "ui/theme/Theme.h"
#include "core/dsp/Decimate.h"

#include <QColor>
#include <QOpenGLContext>
#include <QVector>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace studio::gl {

// ─────────────────────────────────────────────────────────────────────────────
// Shader source
// ─────────────────────────────────────────────────────────────────────────────

static const char* kVertSrc = R"(
#version 330 core
in vec2 aPos;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* kFragSrc = R"(
#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    fragColor = uColor;
}
)";

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────────────────────────────────────

GlWaveformWidget::GlWaveformWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    ensureBuffers();

    renderTimer_ = new QTimer(this);
    renderTimer_->setInterval(33); // ~30 fps
    connect(renderTimer_, &QTimer::timeout, this, [this]() {
        if (!paused_) {
            update();
        }
    });
    // Start will be deferred to after initializeGL succeeds; or started immediately
    // if the widget is shown before GL init.  We start it here so normal usage
    // (on real hardware) works without extra wiring; on offscreen the timer will
    // fire update() but paintGL will hit the !glOk_ fallback branch harmlessly.
    renderTimer_->start();
}

GlWaveformWidget::~GlWaveformWidget()
{
    // Only touch GL resources if the context was successfully initialised and
    // is still available.  On the no-GPU path (glOk_==false, offscreen QPA,
    // CI headless) makeCurrent() may fail silently or crash, so we skip it.
    if (glOk_ && context()) {
        makeCurrent();
        if (vbo_.isCreated()) {
            vbo_.destroy();
        }
        if (vao_.isCreated()) {
            vao_.destroy();
        }
        doneCurrent();
    }
    // program_ is a QObject child — safe to delete without a current context.
    delete program_;
    program_ = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────────────────────────────────────

void GlWaveformWidget::setChannelCount(int n)
{
    channelCount_ = std::max(1, n);
    ensureBuffers();
    ensureVisibleChannels();
    update();
}

void GlWaveformWidget::setChannelLabels(const QStringList& labels)
{
    channelLabels_ = labels;
    update();
}

void GlWaveformWidget::setVisibleChannels(const QVector<int>& channels, const QStringList& labels)
{
    QVector<int> valid;
    valid.reserve(channels.size());
    for (int channel : channels) {
        if (channel >= 0 && channel < channelCount_ && !valid.contains(channel)) {
            valid.push_back(channel);
        }
    }

    if (valid.isEmpty()) {
        valid.reserve(channelCount_);
        for (int c = 0; c < channelCount_; ++c) {
            valid.push_back(c);
        }
    }

    visibleChannels_ = valid;
    channelLabels_ = labels;
    update();
}

void GlWaveformWidget::setSampleRate(double sps)
{
    if (sps > 0.0) {
        sampleRate_ = sps;
    }
    update();
}

void GlWaveformWidget::setWindowSeconds(double s)
{
    if (s > 0.0) {
        windowSec_ = s;
    }
    ensureBuffers();
    update();
}

void GlWaveformWidget::setMicrovoltsPerDiv(double uv)
{
    if (uv > 0.0) {
        uvPerDiv_ = uv;
    }
    update();
}

void GlWaveformWidget::setAutoScale(bool on)
{
    autoScale_ = on;
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
// Data feed
// ─────────────────────────────────────────────────────────────────────────────

void GlWaveformWidget::pushBlock(const std::vector<std::vector<float>>& perChannelSamples)
{
    ensureBuffers();
    const int cap = windowCapacity();
    const int n   = std::min(static_cast<int>(perChannelSamples.size()), channelCount_);

    for (int c = 0; c < n; ++c) {
        auto& buf = channelBufs_[static_cast<size_t>(c)];
        for (float v : perChannelSamples[static_cast<size_t>(c)]) {
            buf.push_back(v);
        }
        // Trim to window capacity
        while (static_cast<int>(buf.size()) > cap) {
            buf.pop_front();
        }
    }
}

void GlWaveformWidget::clearData()
{
    for (auto& buf : channelBufs_) {
        buf.clear();
    }
    // Reset per-channel auto-scale envelopes to the current global µV/div.
    std::fill(autoScaleUv_.begin(), autoScaleUv_.end(), uvPerDiv_);
    update();
}

void GlWaveformWidget::setPaused(bool paused)
{
    paused_ = paused;
    if (!paused_) {
        update();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GL lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void GlWaveformWidget::initializeGL()
{
    // Guard: if there's no valid context, mark as unavailable and bail out.
    if (!context()) {
        glOk_ = false;
        return;
    }

    // initializeOpenGLFunctions() binds GL function pointers to the current context.
    // QOpenGLFunctions::initializeOpenGLFunctions() returns void; we verify via the
    // context itself instead of its return value.
    initializeOpenGLFunctions();

    // Double-check the context is still valid after initialization.
    if (!context() || !context()->isValid()) {
        glOk_ = false;
        return;
    }

    // Build shader program
    program_ = new QOpenGLShaderProgram(this);

    const bool vertOk = program_->addShaderFromSourceCode(QOpenGLShader::Vertex,   kVertSrc);
    const bool fragOk = program_->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragSrc);
    const bool linked = program_->link();

    if (!vertOk || !fragOk || !linked) {
        // Shader compile/link failure — degrade gracefully
        glOk_ = false;
        delete program_;
        program_ = nullptr;
        return;
    }

    // Create VAO (required in Core profile)
    if (!vao_.create()) {
        glOk_ = false;
        delete program_;
        program_ = nullptr;
        return;
    }

    // Create VBO
    vao_.bind();
    vbo_.create();
    vbo_.bind();
    vbo_.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    // Allocate a reasonable initial size (will be reallocated per frame as needed)
    vbo_.allocate(nullptr, static_cast<int>(kMaxCapacity * 2 * sizeof(float)));
    vbo_.release();
    vao_.release();

    glOk_ = true;
}

void GlWaveformWidget::resizeGL(int /*w*/, int /*h*/)
{
    // Viewport is managed automatically by QOpenGLWidget.
}

void GlWaveformWidget::paintGL()
{
    if (!glOk_) {
        // Fallback: draw a message via QPainter (works even when GL is unavailable).
        QPainter p(this);
        p.fillRect(rect(), QColor(theme::kBg));
        p.setPen(QColor(theme::kTextMuted));
        p.drawText(rect(), Qt::AlignCenter, "GPU unavailable — waveform display offline");
        return;
    }

    const QColor bg(theme::kBg);
    glClearColor(static_cast<float>(bg.redF()),
                 static_cast<float>(bg.greenF()),
                 static_cast<float>(bg.blueF()),
                 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Determine max display points (2 × widget width, min 2)
    const int maxPoints = std::max(2, width() * 2);

    program_->bind();
    vao_.bind();
    vbo_.bind();

    // Bind the aPos attribute (location 0)
    program_->enableAttributeArray("aPos");
    program_->setAttributeBuffer("aPos", GL_FLOAT, 0, 2, 0);

    const QColor sigColor(theme::kSignal);
    program_->setUniformValue("uColor",
        static_cast<float>(sigColor.redF()),
        static_cast<float>(sigColor.greenF()),
        static_cast<float>(sigColor.blueF()),
        1.0f);

    ensureVisibleChannels();
    const int displayLaneCount = std::max(1, static_cast<int>(visibleChannels_.size()));

    std::vector<float> pts;
    for (int lane = 0; lane < displayLaneCount; ++lane) {
        const int sourceChannel = visibleChannels_.at(lane);
        pts.clear();
        buildNdcPoints(sourceChannel, lane, displayLaneCount, pts, maxPoints);
        if (pts.size() < 4) {
            continue; // Need at least 2 vertices (each is x,y pair)
        }
        const int byteCount = static_cast<int>(pts.size() * sizeof(float));
        vbo_.bind();
        // Use write() instead of allocate() to avoid reallocation every frame
        if (byteCount > vbo_.size()) {
            vbo_.allocate(pts.data(), byteCount);
        } else {
            vbo_.write(0, pts.data(), byteCount);
        }
        program_->setAttributeBuffer("aPos", GL_FLOAT, 0, 2, 0);
        glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(pts.size() / 2));
    }

    vbo_.release();
    vao_.release();
    program_->release();

    // QPainter overlay: grid, labels, time axis
    drawOverlay(width(), height());
}

// ─────────────────────────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────────────────────────

void GlWaveformWidget::ensureBuffers()
{
    const size_t n = static_cast<size_t>(channelCount_);
    channelBufs_.resize(n);

    // Grow autoScaleUv_ if more channels were added; initialise new entries to
    // the current global µV/div so the first auto-scale frame starts sensibly.
    if (autoScaleUv_.size() < n) {
        autoScaleUv_.resize(n, uvPerDiv_);
    } else if (autoScaleUv_.size() > n) {
        autoScaleUv_.resize(n);
    }
}

void GlWaveformWidget::ensureVisibleChannels()
{
    QVector<int> valid;
    valid.reserve(visibleChannels_.isEmpty() ? channelCount_ : visibleChannels_.size());

    if (visibleChannels_.isEmpty()) {
        for (int c = 0; c < channelCount_; ++c) {
            valid.push_back(c);
        }
    } else {
        for (int channel : visibleChannels_) {
            if (channel >= 0 && channel < channelCount_ && !valid.contains(channel)) {
                valid.push_back(channel);
            }
        }
    }

    if (valid.isEmpty()) {
        for (int c = 0; c < channelCount_; ++c) {
            valid.push_back(c);
        }
    }

    visibleChannels_ = valid;
}

int GlWaveformWidget::windowCapacity() const
{
    const int cap = static_cast<int>(sampleRate_ * windowSec_);
    return std::min(std::max(cap, 2), kMaxCapacity);
}

void GlWaveformWidget::buildNdcPoints(int sourceChannel,
                                      int displayLane,
                                      int displayLaneCount,
                                      std::vector<float>& out,
                                      int maxPoints)
{
    if (sourceChannel < 0 || sourceChannel >= static_cast<int>(channelBufs_.size())
            || displayLane < 0 || displayLaneCount < 1 || displayLane >= displayLaneCount) {
        return;
    }
    const auto& buf = channelBufs_[static_cast<size_t>(sourceChannel)];
    const int   n   = static_cast<int>(buf.size());
    if (n < 1) {
        return;
    }

    // Window capacity W is fixed regardless of how many samples have arrived so
    // far.  The newest sample is always pinned at the right edge (+1); older
    // samples scroll in from the right.  Samples whose position would land left
    // of -1 (i.e. older than the window) are dropped.
    const int W = windowCapacity(); // fixed anchor for X mapping

    // --- Min/max envelope decimation (preserves narrow EEG spikes) -----------
    // Copy the ring buffer into a QVector<double> and call decimateMinMaxIndexed
    // to get both the kept values and their original indices (positions within
    // the n-sample window).
    QVector<double> samples(n);
    for (int i = 0; i < n; ++i) {
        samples[i] = static_cast<double>(buf[static_cast<size_t>(i)]);
    }

    const studio::DecimatedSeries ds = studio::decimateMinMaxIndexed(samples, maxPoints);
    const int kept = ds.values.size();
    if (kept < 1) {
        return;
    }

    // --- Auto-scale: update per-channel envelope from decimated peak ---------
    // We use the decimated set (~2×width points) which is exactly what is
    // visible — cheap and bounded.  Only update when not paused.
    double effectiveUvPerDiv = uvPerDiv_;
    if (autoScale_) {
        if (!paused_) {
            double peak = 0.0;
            for (int k = 0; k < kept; ++k) {
                const double absVal = std::abs(ds.values[k]);
                if (absVal > peak) {
                    peak = absVal;
                }
            }
            autoScaleUv_[static_cast<size_t>(sourceChannel)] =
                autoScaleStep(autoScaleUv_[static_cast<size_t>(sourceChannel)],
                              peak,
                              /*floorUv=*/2.0,
                              /*fillFraction=*/0.85,
                              /*attack=*/0.5,
                              /*release=*/0.05);
        }
        // Per-channel effective µV/div: autoScaleUv_ is the half-lane edge in µV.
        // microvoltsToNdcY uses uvPerDiv * divsPerHalfLane = fullScaleUv, so:
        //   effectiveUvPerDiv = autoScaleUv_[c] / divsPerHalfLane
        effectiveUvPerDiv = autoScaleUv_[static_cast<size_t>(sourceChannel)] / kDivsPerHalfLane;
    }

    out.reserve(static_cast<size_t>(kept) * 2);

    // --- Right-aligned X mapping ----------------------------------------------
    // j=0 is the oldest buffered sample (index 0 in the ring), j=n-1 is newest.
    // The newest sample maps to x = +1.
    // A sample j positions back from the newest maps to:
    //   x = +1 - 2*(n-1-j)/(W-1)
    // Samples with x < -1 are older than the visible window → drop them.
    for (int k = 0; k < kept; ++k) {
        const int   j = ds.indices[k];  // position in the n-sample ring
        const float x = sampleWindowToNdcX(j, n, W);
        if (x < -1.0f) {
            continue; // older than the visible window
        }
        const float y = microvoltsToNdcY(ds.values[k],
                                         displayLane,
                                         displayLaneCount,
                                         effectiveUvPerDiv,
                                         kDivsPerHalfLane);
        out.push_back(x);
        out.push_back(y);
    }
}

void GlWaveformWidget::drawOverlay(int widgetWidth, int widgetHeight)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    ensureVisibleChannels();
    const int displayLaneCount = std::max(1, static_cast<int>(visibleChannels_.size()));

    // Faint horizontal grid lines at each lane boundary
    const QColor gridColor(theme::kBorder);
    QPen gridPen(gridColor, 1, Qt::DotLine);
    p.setPen(gridPen);

    for (int lane = 0; lane <= displayLaneCount; ++lane) {
        const float ndcY = 1.0f - 2.0f * static_cast<float>(lane) / static_cast<float>(displayLaneCount);
        // NDC [-1,1] → widget pixels [0, widgetHeight]
        const int py = static_cast<int>((1.0f - ndcY) * 0.5f * static_cast<float>(widgetHeight));
        p.drawLine(0, py, widgetWidth, py);
    }

    // Channel labels (and optional per-lane scale when auto is on)
    const QColor labelColor(theme::kTextMuted);
    p.setPen(labelColor);
    const QFont  labelFont("monospace", 9);
    p.setFont(labelFont);

    for (int lane = 0; lane < displayLaneCount; ++lane) {
        const int sourceChannel = visibleChannels_.at(lane);
        const float ndcY  = channelCenterNdcY(lane, displayLaneCount);
        const int   py    = static_cast<int>((1.0f - ndcY) * 0.5f * static_cast<float>(widgetHeight));
        const QString label = lane < channelLabels_.size()
                                  ? channelLabels_.at(lane)
                                  : QString("CH%1").arg(sourceChannel + 1);
        p.drawText(4, py + 5, label);

        // When auto-scale is active, show a small ±<scale> µV readout per lane.
        if (autoScale_ && static_cast<size_t>(sourceChannel) < autoScaleUv_.size()) {
            const double scaleUv = autoScaleUv_[static_cast<size_t>(sourceChannel)];
            const QString scaleText = QString("±%1µV").arg(static_cast<int>(scaleUv + 0.5));
            p.drawText(widgetWidth - 72, py + 5, scaleText);
        }
    }

    // Time axis label
    p.drawText(widgetWidth / 2 - 20, widgetHeight - 4,
               QString("%1 s").arg(static_cast<int>(windowSec_)));

    // µV/div annotation (shows "Auto" when auto-scale is on)
    if (autoScale_) {
        p.drawText(widgetWidth - 80, 14, "Auto µV");
    } else {
        p.drawText(widgetWidth - 80, 14,
                   QString("%1 µV/div").arg(static_cast<int>(uvPerDiv_)));
    }

    p.end();
}

} // namespace studio::gl
