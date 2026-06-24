#pragma once
// src/ui/MonitorView.h
//
// MonitorView — real-time 8-channel scrolling EEG trace display.
//
// Uses GlWaveformWidget (QOpenGLWidget + VBO/shader) for GPU-accelerated rendering.
// A 30 Hz QTimer drains the SessionController display buffer, converts counts→µV
// via ScaleConverter, runs each sample through the display-only FilterChain, and
// feeds the result to the GL widget.
//
// The RECORDING path (SessionController→Recorder) is entirely untouched.

#include <QWidget>
#include <QTimer>

#include "core/dsp/DisplayFilterChain.h"

// Forward declarations
class QPushButton;
class QComboBox;
class QLabel;

namespace studio {

class SessionController;

namespace gl {
class GlWaveformWidget;
}

class MonitorView : public QWidget
{
    Q_OBJECT

public:
    explicit MonitorView(SessionController* controller, QWidget* parent = nullptr);
    ~MonitorView() override = default;

    // Reset all channel buffers and time tracking
    void clear();

    // Adjust the assumed sample rate used for the GL widget (default 250 Hz)
    void setSampleRate(int hz);

    // Toggle pause state and update the Pause button's visual state.
    // Safe to call from a QShortcut in MainWindow.
    void togglePause();

public slots:
    void onRenderTick();

private slots:
    void onFilterChanged();

private:
    void buildLayout();
    void rebuildFilterChain();

    // ---- Dependencies -------------------------------------------------------
    SessionController* controller_ = nullptr;

    // ---- GL plot ------------------------------------------------------------
    studio::gl::GlWaveformWidget* glPlot_ = nullptr;

    // ---- Render timer -------------------------------------------------------
    QTimer renderTimer_;

    // ---- Controls -----------------------------------------------------------
    QPushButton* pauseButton_  = nullptr;
    QComboBox*   uvDivCombo_   = nullptr;
    QComboBox*   notchCombo_   = nullptr;  // Off / 50 Hz / 60 Hz
    QComboBox*   bpCombo_      = nullptr;  // Off / bandpass presets
    QLabel*      filterHint_   = nullptr;

    // ---- Display-only filter chain ------------------------------------------
    // Applied ONLY to the values sent to the GL widget. Recording path is untouched.
    studio::dsp::DisplayFilterChain filterChain_;

    // ---- State --------------------------------------------------------------
    bool   paused_         = false;
    int    gains_[8]       = {1,1,1,1,1,1,1,1}; // per-channel ADS1299 PGA gain (from config)
    double uvPerDiv_       = 200.0;              // µV/div from combo box
    int    sampleRateHz_   = 250;

    // ---- Constants ----------------------------------------------------------
    static constexpr double kChannelSpacingUv   = 300.0; // preserved (used by offsetUv callers)
    static constexpr int    kRenderIntervalMs   = 33;    // ~30 fps
    static constexpr int    kNumChannels        = 8;
};

} // namespace studio
