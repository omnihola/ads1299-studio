// src/ui/ImpedanceView.cpp
//
// See ImpedanceView.h for full description.

#include "ui/ImpedanceView.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>

#include "app/SessionController.h"
#include "ui/theme/LedIndicator.h"
#include "ui/theme/Theme.h"

namespace studio {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

ImpedanceView::ImpedanceView(SessionController* controller, QWidget* parent)
    : QWidget(parent)
    , controller_(controller)
{
    buildLayout();

    // Receive lead-off bytes from the controller.
    connect(controller_, &SessionController::metricsUpdated,
            this, &ImpedanceView::onMetricsUpdated);

    // 1-second tick: refresh display + record into log.
    connect(&refreshTimer_, &QTimer::timeout,
            this, &ImpedanceView::onRefreshTick);
    refreshTimer_.setInterval(1000);
    refreshTimer_.start();
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

void ImpedanceView::buildLayout()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(
        theme::spacing(2), theme::spacing(2),
        theme::spacing(2), theme::spacing(2));
    root->setSpacing(theme::spacing(1));

    // Header note — explicitly communicates that values are simulated.
    auto* headerLabel = new QLabel("Impedance (simulated)", this);
    headerLabel->setObjectName("impedanceHeader");
    QFont headerFont = headerLabel->font();
    headerFont.setBold(true);
    headerLabel->setFont(headerFont);
    headerLabel->setStyleSheet(
        QString("color: %1; font-size: 12px;").arg(theme::kTextMuted));
    root->addWidget(headerLabel);

    // One row per channel (0-based internally, labeled CH1..CH8).
    const QString monoStyle =
        QString("font-family: %1; font-size: 11px; color: %2;")
            .arg(theme::kMonoFamily, theme::kTextPrimary);

    for (int ch = 0; ch < 8; ++ch) {
        auto* row    = new QWidget(this);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(theme::spacing(1));

        auto* led = new LedIndicator(row);
        led->setStatus(LedIndicator::Status::Off);
        leds_[static_cast<size_t>(ch)] = led;
        layout->addWidget(led);

        auto* chLabel = new QLabel(QString("CH%1").arg(ch + 1), row);
        chLabel->setStyleSheet(
            QString("color: %1; font-size: 11px;").arg(theme::kTextPrimary));
        layout->addWidget(chLabel, 2);

        auto* kohmLabel = new QLabel("-- kΩ", row);
        kohmLabel->setStyleSheet(monoStyle);
        kohmLabels_[static_cast<size_t>(ch)] = kohmLabel;
        layout->addWidget(kohmLabel, 3);

        root->addWidget(row);
    }

    root->addStretch();
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots
// ─────────────────────────────────────────────────────────────────────────────

void ImpedanceView::onMetricsUpdated(studio::Metrics m)
{
    leadOffP_ = m.leadOffP;
    leadOffN_ = m.leadOffN;
}

void ImpedanceView::onRefreshTick()
{
    elapsedSec_ += 1.0;

    std::array<double, 8> kohmSnapshot{};
    for (int ch = 0; ch < 8; ++ch) {
        const double kOhm  = simulatedKOhm(ch);
        const bool leadOff = ((leadOffP_ >> ch) & 0x01)
                          || ((leadOffN_ >> ch) & 0x01);
        kohmSnapshot[static_cast<size_t>(ch)] = kOhm;
        updateRow(ch, kOhm, leadOff);
    }

    // Record snapshot into history.
    log_.record(elapsedSec_, kohmSnapshot, leadOffP_, leadOffN_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────────────────────────

void ImpedanceView::updateRow(int ch, double kOhm, bool leadOff)
{
    const ImpedanceStatus status = ImpedanceLog::statusFor(kOhm, leadOff);

    // Map ImpedanceStatus → LedIndicator::Status.
    LedIndicator::Status ledStatus = LedIndicator::Status::Off;
    switch (status) {
    case ImpedanceStatus::Ok:    ledStatus = LedIndicator::Status::Ok;    break;
    case ImpedanceStatus::Warn:  ledStatus = LedIndicator::Status::Warn;  break;
    case ImpedanceStatus::Error: ledStatus = LedIndicator::Status::Error; break;
    }

    leds_[static_cast<size_t>(ch)]->setStatus(ledStatus);

    if (leadOff) {
        kohmLabels_[static_cast<size_t>(ch)]->setText("off");
    } else {
        kohmLabels_[static_cast<size_t>(ch)]->setText(
            QString("%1 kΩ").arg(kOhm, 0, 'f', 1));
    }
}

// static
double ImpedanceView::simulatedKOhm(int ch)
{
    // Deterministic, constant per channel. Not measured from hardware.
    // Range 2.0..12.5 kΩ across CH0..CH7.
    return 2.0 + static_cast<double>(ch) * 1.5;
}

} // namespace studio
