#pragma once
// src/ui/SpectrumView.h
//
// SpectrumView — live power-spectrum display for a selected EEG channel.
//
// A ~200ms QTimer polls SessionController::recentSamples(), runs FftProcessor,
// converts to dB, and updates a QCustomPlot.  A channel combo-box and a dB/linear
// toggle are provided in a thin toolbar above the plot.

#include <QWidget>
#include <memory>

class QComboBox;
class QCheckBox;
class QTimer;
class QCustomPlot;

namespace studio {

class SessionController;
class FftProcessor;

class SpectrumView : public QWidget
{
    Q_OBJECT

public:
    explicit SpectrumView(SessionController* controller,
                          QWidget* parent = nullptr);
    ~SpectrumView() override;

private slots:
    void onTimer();

private:
    void buildUi();
    void applyTheme();

    SessionController*        controller_ = nullptr;
    QComboBox*                channelBox_ = nullptr;
    QCheckBox*                dbToggle_   = nullptr;
    QCustomPlot*              plot_       = nullptr;
    QTimer*                   timer_      = nullptr;
    std::unique_ptr<FftProcessor> fft_;

    static constexpr int kNfft = 512;
};

} // namespace studio
