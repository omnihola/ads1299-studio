#pragma once
// src/ui/MainWindow.h
//
// MainWindow — integration hub for ADS1299 Studio.
// Owns a SessionController, builds the top-level window layout, and wires
// controller signals to the status bar widgets.

#include <QMainWindow>
#include <QTabWidget>
#include <QLabel>

#include "app/AppState.h"          // studio::State, studio::Metrics
#include "ui/theme/LedIndicator.h" // studio::LedIndicator

class QAction;
class QTimer;

namespace studio {

class SessionController;
class RecordingPanel;
class ImpedanceView;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(SessionController* controller = nullptr,
                        QWidget* parent = nullptr);
    ~MainWindow() override;

    // Accessor so later tasks can swap tab contents
    QTabWidget* tabs() const { return tabs_; }

    // Programmatically start streaming (used by --shot headless screenshot path)
    void startStreamingForDemo();

private slots:
    void onStartToggled(bool on);
    void onMetricsUpdated(studio::Metrics m);
    void onStateChanged(studio::State s);
    void onErrorOccurred(const QString& message);
    void refreshDiskLabel();

private:
    void buildToolbar();
    void buildDock();
    void buildTabs();
    void buildStatusBar();
    void wireController();

    // ---- Owned controller ---------------------------------------------------
    SessionController* controller_ = nullptr;

    // ---- Toolbar actions ----------------------------------------------------
    QAction* connectAction_  = nullptr;
    QAction* startAction_    = nullptr;
    QAction* recordAction_   = nullptr;

    // ---- Central tab widget -------------------------------------------------
    QTabWidget* tabs_ = nullptr;

    // ---- Recording panel (lives in the "Recording" tab) ---------------------
    RecordingPanel* recordingPanel_ = nullptr;

    // ---- Impedance view (lives in the "Impedance" tab) ----------------------
    ImpedanceView*  impedanceView_  = nullptr;

    // ---- Status bar widgets -------------------------------------------------
    LedIndicator* linkLed_      = nullptr;
    QLabel*       spsLabel_     = nullptr;
    QLabel*       droppedLabel_ = nullptr;
    QLabel*       bufferLabel_  = nullptr;
    QLabel*       diskLabel_    = nullptr;

    // ---- Disk refresh timer -------------------------------------------------
    QTimer* diskTimer_ = nullptr;
};

} // namespace studio
