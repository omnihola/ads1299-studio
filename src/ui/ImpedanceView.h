#pragma once
// src/ui/ImpedanceView.h
//
// ImpedanceView — per-channel electrode impedance status display.
//
// Shows 8 rows (CH1..CH8). Each row has a LedIndicator (colour driven by
// ImpedanceLog::statusFor), a channel label, and a kΩ readout.
//
// IMPORTANT: kΩ values are SYNTHESISED (not measured from hardware). They are
// computed as a stable, deterministic value per channel (2.0 + ch * 1.5 kΩ).
// A "Impedance (simulated)" header makes this explicit in the UI.
//
// The widget subscribes to SessionController::metricsUpdated to get
// leadOffP/leadOffN bits. A 1-second QTimer triggers each display refresh and
// records a snapshot into an internal ImpedanceLog.

#include <QWidget>
#include <QTimer>
#include <array>
#include <cstdint>

#include "app/AppState.h"
#include "core/recording/ImpedanceLog.h"

class QLabel;

namespace studio {

class SessionController;
class LedIndicator;

class ImpedanceView : public QWidget
{
    Q_OBJECT

public:
    explicit ImpedanceView(SessionController* controller, QWidget* parent = nullptr);
    ~ImpedanceView() override = default;

private slots:
    void onMetricsUpdated(studio::Metrics m);
    void onRefreshTick();

private:
    void buildLayout();
    void updateRow(int ch, double kOhm, bool leadOff);

    // Returns the simulated kΩ for channel ch (0-based).
    // Formula: 2.0 + ch * 1.5 — deterministic, does not change per tick.
    static double simulatedKOhm(int ch);

    // ---- Dependencies -------------------------------------------------------
    SessionController* controller_ = nullptr;

    // ---- Per-channel widgets (indices 0..7) ---------------------------------
    std::array<LedIndicator*, 8> leds_       {};
    std::array<QLabel*,       8> kohmLabels_ {};

    // ---- Cached lead-off state from last metricsUpdated --------------------
    uint8_t leadOffP_ = 0;
    uint8_t leadOffN_ = 0;

    // ---- Internal history ---------------------------------------------------
    ImpedanceLog log_;

    // ---- Time tracking (seconds since first tick) ---------------------------
    double elapsedSec_ = 0.0;

    // ---- Refresh timer (1 Hz) -----------------------------------------------
    QTimer refreshTimer_;
};

} // namespace studio
