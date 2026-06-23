#pragma once
// src/ui/MonitorView.h
//
// MonitorView — real-time 8-channel scrolling EEG trace display.
//
// Uses QCustomPlot for rendering. 8 QCPGraph instances are stacked vertically
// using fixed µV offsets. A 30 Hz QTimer drains the SessionController display
// buffer, converts counts→µV via ScaleConverter, and redraws the plot.

#include <QWidget>
#include <QVector>
#include <QTimer>

// Forward declarations
class QCustomPlot;
class QCPGraph;
class QPushButton;
class QComboBox;

namespace studio {

class SessionController;

class MonitorView : public QWidget
{
    Q_OBJECT

public:
    explicit MonitorView(SessionController* controller, QWidget* parent = nullptr);
    ~MonitorView() override = default;

    // Reset all channel buffers and time tracking
    void clear();

    // Adjust the assumed sample rate used for time axis (default 250 Hz)
    void setSampleRate(int hz);

public slots:
    void onRenderTick();

private:
    void buildLayout();
    void stylePlot();
    void setupGraphs();
    double offsetUv(int channel) const;
    void applyYScale();

    // ---- Dependencies -------------------------------------------------------
    SessionController* controller_ = nullptr;

    // ---- Plot ---------------------------------------------------------------
    QCustomPlot* plot_                  = nullptr;
    QCPGraph*    graphs_[8]             = {};

    // ---- Per-channel rolling sample buffers ---------------------------------
    QVector<double> channelSamples_[8]; // values in µV + offset
    QVector<double> timeSamples_;       // shared time axis (seconds)

    // ---- Time tracking ------------------------------------------------------
    double currentTimeSec_ = 0.0;
    int    sampleRateHz_   = 250;

    // ---- Render timer -------------------------------------------------------
    QTimer renderTimer_;

    // ---- Controls -----------------------------------------------------------
    QPushButton* pauseButton_  = nullptr;
    QComboBox*   uvDivCombo_   = nullptr;

    // ---- State --------------------------------------------------------------
    bool   paused_         = false;
    int    gains_[8]       = {1,1,1,1,1,1,1,1}; // per-channel ADS1299 PGA gain (from config)
    double uvPerDiv_       = 200.0;      // µV/div from combo box

    // ---- Constants ----------------------------------------------------------
    static constexpr double kWindowSeconds      = 5.0;
    static constexpr double kChannelSpacingUv   = 300.0; // µV between channel baselines
    static constexpr int    kRenderIntervalMs   = 33;    // ~30 fps
};

} // namespace studio
