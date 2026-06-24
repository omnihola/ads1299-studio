#pragma once
// src/ui/AcquisitionPanel.h
//
// AcquisitionPanel — always-visible left-dock panel that exposes sample rate
// and gain as first-class controls, with live resolution / input-range /
// bandwidth readouts derived from DeviceConfig math.
//
// Namespace: studio

#include <QWidget>
#include <QLabel>
#include <QComboBox>

#include "core/device/DeviceConfig.h"

namespace studio {

class SessionController;

class AcquisitionPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AcquisitionPanel(SessionController* controller,
                              QWidget* parent = nullptr);

    // ── Pure static helpers (exposed for unit testing) ────────────────────────
    /// LSB size in µV: (4.5 / (gain * 2^23)) * 1e6
    static double resolutionMicrovolts(int gain);

    /// Full-scale input range in mV (±): (4.5 / gain) * 1e3
    static double inputRangeMillivolts(int gain);

    /// Nyquist bandwidth in Hz: sps / 2.0
    static double nyquistHz(int sps);

    // ── Test hook ─────────────────────────────────────────────────────────────
    /// Programmatically select a sample rate in the combo (by SPS value).
    /// Triggers the same applyConfig path as a real user interaction.
    void setSampleRateSelection(int sps);

private slots:
    void onComboChanged();
    void onConfigChanged(studio::DeviceConfig cfg);

private:
    void buildUi();
    void refreshReadouts(int gain, int sps);

    SessionController* controller_ = nullptr;

    QComboBox* spsCombo_  = nullptr;
    QComboBox* gainCombo_ = nullptr;

    QLabel* resolutionLabel_  = nullptr;
    QLabel* inputRangeLabel_  = nullptr;
    QLabel* bandwidthLabel_   = nullptr;
};

} // namespace studio
