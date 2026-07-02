#pragma once
// src/ui/MainWindow.h
//
// MainWindow — integration hub for ADS1299 Studio.
// Owns a SessionController, builds the top-level window layout, and wires
// controller signals to the status bar widgets.

#include <QMainWindow>
#include <QTabWidget>
#include <QLabel>
#include <QStringList>

#include "app/AppState.h"          // studio::State, studio::Metrics
#include "ui/theme/LedIndicator.h" // studio::LedIndicator
#include "ui/AlertBar.h"           // studio::AlertBar

class QAction;
class QShortcut;
class QTimer;

namespace studio {

class SessionController;
class MonitorView;
class RecordingPanel;
class ImpedanceView;
class SessionsView;

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

    // Label of the "built-in simulator" entry offered by the Connect fallback
    // prompt, so the user can always return to the default source.
    static QString simulatorOptionLabel();

protected:
    // Persist window geometry on close (restored in the constructor).
    void closeEvent(QCloseEvent* event) override;

    // Ask the user to pick from @p options (serial ports + the simulator
    // entry). Returns the picked option or an empty string on cancel.
    // Virtual so tests can drive the Connect flow without a modal dialog.
    virtual QString promptSerialPort(const QStringList& options);

private slots:
    void onConnectTriggered();
    void onStartToggled(bool on);
    void onMetricsUpdated(studio::Metrics m);
    void onStateChanged(studio::State s);
    void onSourceChanged(studio::SourceType type, const QString& name);
    void onErrorOccurred(const QString& message);
    void refreshDiskLabel();

private:
    void buildToolbar();
    void buildDock();
    void buildTabs();
    void buildStatusBar();
    void wireController();

    // Persistent connection readout (LED + status label) and action gating.
    void setConnectionStatus(LedIndicator::Status led, const QString& text);
    void updateControlGates();

    // ---- Owned controller ---------------------------------------------------
    SessionController* controller_ = nullptr;

    // ---- Toolbar actions ----------------------------------------------------
    QAction* connectAction_  = nullptr;
    QAction* startAction_    = nullptr;
    QAction* recordAction_   = nullptr;

    // ---- Alert bar (above tabs) ---------------------------------------------
    AlertBar* alertBar_ = nullptr;

    // ---- Central tab widget -------------------------------------------------
    QTabWidget* tabs_ = nullptr;

    // ---- Monitor view (lives in the "Monitor" tab) --------------------------
    MonitorView*    monitorView_    = nullptr;

    // ---- Recording panel (lives in the "Recording" tab) ---------------------
    RecordingPanel* recordingPanel_ = nullptr;

    // ---- Impedance view (lives in the "Impedance" tab) ----------------------
    ImpedanceView*  impedanceView_  = nullptr;

    // ---- Sessions view (lives in the "Sessions" tab) ------------------------
    SessionsView*   sessionsView_   = nullptr;

    // ---- Status bar widgets -------------------------------------------------
    LedIndicator* linkLed_      = nullptr;
    QLabel*       connLabel_    = nullptr;   // persistent connection status
    QLabel*       spsLabel_     = nullptr;
    QLabel*       droppedLabel_ = nullptr;
    QLabel*       bufferLabel_  = nullptr;
    QLabel*       diskLabel_    = nullptr;

    // ---- Connection state (drives Start/Connect gating) ---------------------
    bool deviceLinkUp_ = false;   // last hardware connect succeeded & no error since
    bool connecting_   = false;   // a connect attempt is in flight

    // ---- Disk refresh timer -------------------------------------------------
    QTimer* diskTimer_ = nullptr;
};

} // namespace studio
