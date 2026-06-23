#pragma once
// src/ui/RegistersView.h
//
// RegistersView — GUI editor for the ADS1299 DeviceConfig.
// Shows a per-channel gain/mux grid, global sample-rate selector,
// SRB1 / BIAS-drive toggles, a live register-byte hex preview, and
// an "Apply" button that pushes the current config to the controller.

#include <QWidget>
#include "core/device/DeviceConfig.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace studio {

class SessionController;

class RegistersView : public QWidget
{
    Q_OBJECT

public:
    explicit RegistersView(SessionController* controller, QWidget* parent = nullptr);

private slots:
    void onAnyControlChanged();
    void onApplyClicked();

private:
    // Build a DeviceConfig immutably from the current state of all controls.
    DeviceConfig currentConfig() const;

    // Refresh the hex-dump preview label from the current controls.
    void refreshPreview();

    SessionController* controller_ = nullptr;

    // Global sample-rate selector
    QComboBox* sampleRateCombo_ = nullptr;

    // Per-channel combos (index 0 = CH1 .. 7 = CH8)
    QComboBox* gainCombo_[8] = {};
    QComboBox* muxCombo_[8]  = {};

    // Reference / BIAS options
    QCheckBox* srb1Check_      = nullptr;
    QCheckBox* biasEnabledCheck_ = nullptr;

    // Live register hex preview
    QPlainTextEdit* hexPreview_ = nullptr;

    // Apply button
    QPushButton* applyButton_ = nullptr;
};

} // namespace studio
