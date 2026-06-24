// src/ui/AlertBar.cpp
//
// See AlertBar.h for public contract.

#include "ui/AlertBar.h"

#include <QHBoxLayout>
#include <QLabel>

#include "app/SessionController.h"
#include "ui/theme/Theme.h"

namespace studio {

// ── Construction ─────────────────────────────────────────────────────────────

AlertBar::AlertBar(SessionController* controller, QWidget* parent)
    : QWidget(parent)
{
    // Fixed height so the bar doesn't expand
    setFixedHeight(26);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(8);

    // Small dot indicator
    dot_ = new QWidget(this);
    dot_->setFixedSize(10, 10);
    // Rounded via stylesheet — set in refresh()
    layout->addWidget(dot_);

    label_ = new QLabel(this);
    layout->addWidget(label_);
    layout->addStretch();

    // Wire controller signals
    if (controller) {
        connect(controller, &SessionController::metricsUpdated,
                this, &AlertBar::onMetricsUpdated);
        connect(controller, &SessionController::errorOccurred,
                this, &AlertBar::onErrorOccurred);
        connect(controller, &SessionController::stateChanged,
                this, &AlertBar::onStateChanged);
    }

    refresh();
}

// ── Pure static summarizer ────────────────────────────────────────────────────

AlertSummary AlertBar::summarize(const QString& lastError,
                                 uint64_t       droppedSamples,
                                 uint8_t        leadOffP,
                                 uint8_t        leadOffN)
{
    // Priority 1: explicit error message
    if (!lastError.isEmpty()) {
        return { AlertLevel::Error, lastError };
    }

    // Priority 2: dropped samples
    if (droppedSamples > 0) {
        return { AlertLevel::Error,
                 QString("%1 dropped sample(s) \xe2\x80\x94 data gap")
                     .arg(droppedSamples) };
    }

    // Priority 3: lead-off (any P or N bit set)
    const uint8_t combined = static_cast<uint8_t>(leadOffP | leadOffN);
    if (combined != 0) {
        QStringList channels;
        for (int c = 0; c < 8; ++c) {
            if (combined & (1u << c)) {
                channels << QString("CH%1").arg(c + 1);
            }
        }
        return { AlertLevel::Warn,
                 QString("Lead-off: %1").arg(channels.join(", ")) };
    }

    // Priority 4: all clear
    return { AlertLevel::Ok, "Signal OK" };
}

// ── Test hook ─────────────────────────────────────────────────────────────────

void AlertBar::applyForTest(const QString& lastError,
                            uint64_t       droppedSamples,
                            uint8_t        leadOffP,
                            uint8_t        leadOffN)
{
    lastError_      = lastError;
    droppedSamples_ = droppedSamples;
    leadOffP_       = leadOffP;
    leadOffN_       = leadOffN;
    refresh();
}

// ── Private slots ─────────────────────────────────────────────────────────────

void AlertBar::onMetricsUpdated(studio::Metrics m)
{
    droppedSamples_ = m.droppedSamples;
    leadOffP_       = m.leadOffP;
    leadOffN_       = m.leadOffN;
    refresh();
}

void AlertBar::onErrorOccurred(const QString& message)
{
    lastError_ = message;
    refresh();
}

void AlertBar::onStateChanged(studio::State newState)
{
    // Only clear alerts on a genuine fresh start: Idle → Streaming.
    // Transitions such as Streaming → Recording or Recording → Streaming
    // (stop-record) must NOT wipe accumulated errors/drops.
    if (newState == State::Streaming && previousState_ == State::Idle) {
        lastError_.clear();
        // droppedSamples_ is now driven purely from metricsUpdated; the
        // controller resets its counter at startStreaming() (FIX 1), so the
        // first metricsUpdated after a fresh start will already carry 0.
        droppedSamples_ = 0;
        refresh();
    }
    previousState_ = newState;
}

// ── refresh ───────────────────────────────────────────────────────────────────

void AlertBar::refresh()
{
    summary_ = summarize(lastError_, droppedSamples_, leadOffP_, leadOffN_);

    const char* bgColor   = nullptr;
    const char* textColor = nullptr;
    const char* dotColor  = nullptr;

    switch (summary_.level) {
    case AlertLevel::Ok:
        // Subtle: use surface background, muted dot and text
        bgColor   = theme::kSurface;
        textColor = theme::kTextMuted;
        dotColor  = theme::kOk;
        break;
    case AlertLevel::Warn:
        bgColor   = theme::kWarnBg;
        textColor = theme::kWarn;
        dotColor  = theme::kWarn;
        break;
    case AlertLevel::Error:
        bgColor   = theme::kErrorBg;
        textColor = theme::kError;
        dotColor  = theme::kError;
        break;
    }

    setStyleSheet(
        QString("AlertBar { background-color: %1; border-bottom: 1px solid %2; }")
        .arg(bgColor, theme::kBorder));

    if (dot_) {
        dot_->setStyleSheet(
            QString("background-color: %1; border-radius: 5px;").arg(dotColor));
    }

    if (label_) {
        label_->setStyleSheet(
            QString("color: %1; font-family: %2; font-size: 12px;")
            .arg(textColor, theme::kMonoFamily));
        label_->setText(summary_.text);
    }
}

} // namespace studio
