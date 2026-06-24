#pragma once
// src/ui/gl/GlWaveformWidget.h
//
// QOpenGLWidget-based GPU line renderer for multi-channel EEG waveforms.
// Replaces QCustomPlot in MonitorView for high sample-rate / high-channel-count
// throughput.
//
// Key design points:
//  - CPU computes NDC coords via WaveformTransform helpers (tested independently)
//  - Trivial passthrough vertex shader; fragment shader uses a uniform colour
//  - QPainter overlay drawn after GL pass (grid, labels, axes)
//  - On GL unavailable (offscreen QPA / headless CI) the widget degrades to a
//    no-op: initializeGL sets glOk_=false, paintGL draws a fallback text via
//    QPainter and returns. The app NEVER crashes.

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QTimer>

#include <deque>
#include <vector>

namespace studio::gl {

class GlWaveformWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit GlWaveformWidget(QWidget* parent = nullptr);
    ~GlWaveformWidget() override;

    // ---- Configuration -------------------------------------------------------
    void setChannelCount(int n);
    void setSampleRate(double sps);
    void setWindowSeconds(double s);        // visible scroll window (default 5 s)
    void setMicrovoltsPerDiv(double uv);    // vertical scale (default 200 µV/div)
    void setAutoScale(bool on);             // per-channel auto-scale (fast-attack/slow-release)

    // ---- Data feed -----------------------------------------------------------
    // Append a block of samples.  perChannelSamples[c] contains the new µV values
    // for channel c.  Channels beyond channelCount_ are ignored.
    void pushBlock(const std::vector<std::vector<float>>& perChannelSamples);

    void clearData();
    void setPaused(bool paused);

    // Returns true if the GL context initialised successfully.
    bool glReady() const { return glOk_; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    // GL resources
    QOpenGLShaderProgram* program_  = nullptr;
    QOpenGLVertexArrayObject vao_;
    QOpenGLBuffer            vbo_{ QOpenGLBuffer::VertexBuffer };

    // State
    bool   glOk_        = false;
    bool   paused_      = false;
    bool   autoScale_   = false;
    int    channelCount_= 8;
    double sampleRate_  = 250.0;
    double windowSec_   = 5.0;
    double uvPerDiv_    = 200.0;

    // Per-channel auto-scale envelope (µV at the lane edge)
    std::vector<double> autoScaleUv_;   // size == channelCount_

    static constexpr double kDivsPerHalfLane = 1.5;
    static constexpr int    kMaxCapacity     = 200000; // per channel

    // Per-channel rolling buffers
    std::vector<std::deque<float>> channelBufs_;

    // Render timer (~33 ms → ~30 fps)
    QTimer* renderTimer_ = nullptr;

    // Helpers
    void ensureBuffers();
    int  windowCapacity() const;            // sampleRate_ * windowSec_, capped
    void buildNdcPoints(int channel, std::vector<float>& out, int maxPoints);
    void drawOverlay(int widgetWidth, int widgetHeight);
};

} // namespace studio::gl
