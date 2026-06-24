#pragma once
// src/ui/AlertBar.h
//
// AlertBar — a prominent horizontal status bar shown above the central tab
// widget.  It summarises the highest-priority data-quality problem into a
// single colour-coded line so researchers see problems at a glance.
//
//   Priority (highest first):
//     1. Non-empty lastError   → Error  (red)
//     2. droppedSamples > 0    → Error  (red)
//     3. Any lead-off bit set  → Warn   (amber)
//     4. Everything clean      → Ok     (green, subtle)

#include <QWidget>
#include <QString>
#include <cstdint>

#include "app/AppState.h"   // studio::Metrics, studio::State

class QLabel;   // forward declaration in global scope

namespace studio {

class SessionController;

// ── Alert level & summary ────────────────────────────────────────────────────

enum class AlertLevel { Ok, Warn, Error };

struct AlertSummary {
    AlertLevel level = AlertLevel::Ok;
    QString    text;
};

// ── AlertBar widget ──────────────────────────────────────────────────────────

class AlertBar : public QWidget
{
    Q_OBJECT

public:
    explicit AlertBar(SessionController* controller, QWidget* parent = nullptr);

    // Current displayed summary (for tests and external queries).
    AlertSummary currentSummary() const { return summary_; }

    // Pure static summarizer — callable without constructing the widget.
    // Priority: lastError > droppedSamples > lead-off > ok.
    static AlertSummary summarize(const QString& lastError,
                                  uint64_t       droppedSamples,
                                  uint8_t        leadOffP,
                                  uint8_t        leadOffN);

    // Test hook: store state directly and refresh without needing Qt signals.
    void applyForTest(const QString& lastError,
                      uint64_t       droppedSamples,
                      uint8_t        leadOffP,
                      uint8_t        leadOffN);

private slots:
    void onMetricsUpdated(studio::Metrics m);
    void onErrorOccurred(const QString& message);
    void onStateChanged(studio::State state);

private:
    void refresh();

    // ── Persisted state ──────────────────────────────────────────────────────
    QString      lastError_;
    uint64_t     droppedSamples_ = 0;
    uint8_t      leadOffP_       = 0;
    uint8_t      leadOffN_       = 0;
    studio::State previousState_ = studio::State::Idle;

    // ── Computed summary ─────────────────────────────────────────────────────
    AlertSummary summary_;

    // ── Child widgets ────────────────────────────────────────────────────────
    QWidget* dot_   = nullptr;   // small coloured circle
    QLabel*  label_ = nullptr;
};

} // namespace studio
