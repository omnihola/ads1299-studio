// src/ui/gl/GlWaveformWidget.cpp
//
// See GlWaveformWidget.h for the public contract.

#include "ui/gl/GlWaveformWidget.h"
#include "ui/gl/WaveformTransform.h"
#include "ui/theme/Theme.h"

#include <QColor>
#include <QOpenGLContext>
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
    // Destroy GL resources while the context is current.
    makeCurrent();
    if (program_) {
        delete program_;
        program_ = nullptr;
    }
    if (vbo_.isCreated()) {
        vbo_.destroy();
    }
    if (vao_.isCreated()) {
        vao_.destroy();
    }
    doneCurrent();
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────────────────────────────────────

void GlWaveformWidget::setChannelCount(int n)
{
    channelCount_ = std::max(1, n);
    ensureBuffers();
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

    std::vector<float> pts;
    for (int c = 0; c < channelCount_; ++c) {
        pts.clear();
        buildNdcPoints(c, pts, maxPoints);
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
    channelBufs_.resize(static_cast<size_t>(channelCount_));
}

int GlWaveformWidget::windowCapacity() const
{
    const int cap = static_cast<int>(sampleRate_ * windowSec_);
    return std::min(std::max(cap, 2), kMaxCapacity);
}

void GlWaveformWidget::buildNdcPoints(int channel,
                                      std::vector<float>& out,
                                      int maxPoints) const
{
    if (channel < 0 || channel >= static_cast<int>(channelBufs_.size())) {
        return;
    }
    const auto& buf = channelBufs_[static_cast<size_t>(channel)];
    const int   n   = static_cast<int>(buf.size());
    if (n < 1) {
        return;
    }

    // Decide stride for decimation: keep at most maxPoints/2 vertices per
    // channel (each vertex is 2 floats). Use stride-based decimation (take
    // every stride-th sample) — sufficient for display; min/max would be
    // better but adds complexity; keep it simple (KISS).
    const int targetPts = std::max(2, maxPoints / 2);
    const int stride    = std::max(1, n / targetPts);
    const int numOut    = (n + stride - 1) / stride; // ceil division

    out.reserve(static_cast<size_t>(numOut) * 2);

    // We need to know the total rendered samples for X mapping.
    // We map the actual sample positions to [0..N-1] where N is n.
    for (int i = 0; i < n; i += stride) {
        const float x = sampleIndexToNdcX(i, n);
        const float y = microvoltsToNdcY(static_cast<double>(buf[static_cast<size_t>(i)]),
                                         channel,
                                         channelCount_,
                                         uvPerDiv_,
                                         kDivsPerHalfLane);
        out.push_back(x);
        out.push_back(y);
    }
}

void GlWaveformWidget::drawOverlay(int widgetWidth, int widgetHeight)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Faint horizontal grid lines at each lane boundary
    const QColor gridColor(theme::kBorder);
    QPen gridPen(gridColor, 1, Qt::DotLine);
    p.setPen(gridPen);

    for (int c = 0; c <= channelCount_; ++c) {
        // lane boundary at NDC y = 1 - 2*c/channelCount_
        const float ndcY = 1.0f - 2.0f * static_cast<float>(c) / static_cast<float>(channelCount_);
        // NDC [-1,1] → widget pixels [0, widgetHeight]
        const int py = static_cast<int>((1.0f - ndcY) * 0.5f * static_cast<float>(widgetHeight));
        p.drawLine(0, py, widgetWidth, py);
    }

    // Channel labels
    const QColor labelColor(theme::kTextMuted);
    p.setPen(labelColor);
    const QFont  labelFont("monospace", 9);
    p.setFont(labelFont);

    for (int c = 0; c < channelCount_; ++c) {
        const float ndcY  = channelCenterNdcY(c, channelCount_);
        const int   py    = static_cast<int>((1.0f - ndcY) * 0.5f * static_cast<float>(widgetHeight));
        p.drawText(4, py + 5, QString("CH%1").arg(c + 1));
    }

    // Time axis label
    p.drawText(widgetWidth / 2 - 20, widgetHeight - 4,
               QString("%1 s").arg(static_cast<int>(windowSec_)));

    // µV/div annotation
    p.drawText(widgetWidth - 80, 14,
               QString("%1 µV/div").arg(static_cast<int>(uvPerDiv_)));

    p.end();
}

} // namespace studio::gl
