// src/ui/MainWindow.cpp
//
// See MainWindow.h for public contract.

#include "ui/MainWindow.h"

#include <QAction>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QInputDialog>
#include <QLabel>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QWidget>
#include <QDir>
#include <QStorageInfo>
#include <QShortcut>
#include <QKeySequence>
#include <QSignalBlocker>
#include <QSettings>
#include <QCloseEvent>
#include "app/SettingsKeys.h"

#include "app/SessionController.h"
#include "core/acquisition/SerialSource.h"
#include "core/acquisition/SimulatedSource.h"
#include "ui/AlertBar.h"
#include "ui/AcquisitionPanel.h"
#include "ui/ImpedanceView.h"
#include "ui/MonitorView.h"
#include "ui/RecordingPanel.h"
#include "ui/RegistersView.h"
#include "ui/SessionsView.h"
#include "ui/ShortcutsHelpDialog.h"
#include "ui/SpectrumView.h"

namespace studio {

// ──────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ──────────────────────────────────────────────────────────────────────────────

MainWindow::MainWindow(SessionController* controller, QWidget* parent)
    : QMainWindow(parent)
{
    // Take ownership: if none supplied, create a default SimulatedSource-based one.
    if (controller) {
        controller_ = controller;
        controller_->setParent(this);
    } else {
        controller_ = new SessionController(new SimulatedSource(), /*displayBufferCapacity=*/65536, this);
    }

    setWindowTitle("ADS1299 Studio");
    resize(1280, 800);

    buildToolbar();
    buildDock();
    buildTabs();
    buildStatusBar();
    wireController();

    refreshDiskLabel();

    // Refresh disk label every 5 s
    diskTimer_ = new QTimer(this);
    diskTimer_->setInterval(5000);
    connect(diskTimer_, &QTimer::timeout, this, &MainWindow::refreshDiskLabel);
    diskTimer_->start();

    // Ctrl+M — add event marker (replaces bare Qt::Key_M to avoid firing in text fields).
    // SessionController::addMarker is a no-op unless recording, so this is safe always.
    auto* markerShortcut = new QShortcut(QKeySequence("Ctrl+M"), this);
    connect(markerShortcut, &QShortcut::activated, this, [this]() {
        controller_->addMarker("marker");
    });

    // Ctrl+P — pause / resume the Monitor display
    auto* pauseShortcut = new QShortcut(QKeySequence("Ctrl+P"), this);
    connect(pauseShortcut, &QShortcut::activated, this, [this]() {
        if (monitorView_) {
            monitorView_->togglePause();
        }
    });

    // Ctrl+1..Ctrl+5 — switch between the five main tabs
    for (int i = 0; i < 5; ++i) {
        auto* sc = new QShortcut(QKeySequence(QString("Ctrl+%1").arg(i + 1)), this);
        connect(sc, &QShortcut::activated, this, [this, i]() {
            tabs_->setCurrentIndex(i);
        });
    }

    // F1 — open the keyboard shortcuts help dialog
    auto* helpShortcut = new QShortcut(QKeySequence(Qt::Key_F1), this);
    connect(helpShortcut, &QShortcut::activated, this, [this]() {
        ShortcutsHelpDialog dlg(this);
        dlg.exec();
    });

    // Restore the last window size/position (if any). Empty on first launch →
    // restoreGeometry is a no-op and the default resize(1280, 800) above stands.
    const QByteArray geo = QSettings().value(settings::kWindowGeometry).toByteArray();
    if (!geo.isEmpty()) {
        restoreGeometry(geo);
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Persist window size/position so the app reopens where the user left it.
    QSettings().setValue(settings::kWindowGeometry, saveGeometry());
    QMainWindow::closeEvent(event);
}

// ──────────────────────────────────────────────────────────────────────────────
// Private builders
// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::buildToolbar()
{
    auto* toolbar = addToolBar("Main Toolbar");
    toolbar->setObjectName("mainToolbar");
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    // Flat style: no border, minimal padding — overridden by QSS if present
    toolbar->setStyleSheet("QToolBar { border: none; spacing: 4px; }");

    connectAction_ = new QAction("Connect", this);
    connectAction_->setToolTip("Connect to device");

    startAction_ = new QAction("Start", this);
    startAction_->setCheckable(true);
    startAction_->setShortcut(QKeySequence(Qt::Key_F5));
    startAction_->setToolTip("Start / stop streaming (F5)");

    recordAction_ = new QAction("Record", this);
    recordAction_->setCheckable(true);
    recordAction_->setShortcut(QKeySequence("Ctrl+R"));
    recordAction_->setToolTip("Start / stop recording (Ctrl+R)");

    auto* helpAction = new QAction("Shortcuts", this);
    helpAction->setToolTip("Keyboard shortcuts help (F1)");
    connect(helpAction, &QAction::triggered, this, [this]() {
        ShortcutsHelpDialog dlg(this);
        dlg.exec();
    });

    toolbar->addAction(connectAction_);
    toolbar->addSeparator();
    toolbar->addAction(startAction_);
    toolbar->addAction(recordAction_);
    toolbar->addSeparator();
    toolbar->addAction(helpAction);

    connect(startAction_, &QAction::toggled, this, &MainWindow::onStartToggled);

    // Connect action: list available serial ports and swap the active source.
    connect(connectAction_, &QAction::triggered, this, [this]() {
        // Prefer MMB0 USB device (ADS1299 via libusb, VID=0x0451 PID=0x5718).
        if (controller_->connectMmb0()) {
            linkLed_->setStatus(LedIndicator::Status::Ok);
            statusBar()->showMessage("Connected: ADS1299 (MMB0)", 4000);
            return;
        }

        // Fall back to serial port selection.
        const QStringList ports = SerialSource::availablePorts();
        if (ports.isEmpty()) {
            linkLed_->setStatus(LedIndicator::Status::Warn);
            statusBar()->showMessage(
                "No MMB0 (0451:5718) or serial device found", 5000);
            return;
        }
        bool ok = false;
        const QString port = QInputDialog::getItem(
            this, "Connect to Device", "Select serial port:", ports,
            /*current=*/0, /*editable=*/false, &ok);
        if (!ok || port.isEmpty()) return;

        const bool connected = controller_->connectSerial(port);
        if (connected) {
            linkLed_->setStatus(LedIndicator::Status::Ok);
            statusBar()->showMessage(QString("Connected to %1").arg(port), 4000);
        } else {
            linkLed_->setStatus(LedIndicator::Status::Error);
            statusBar()->showMessage(
                QString("Failed to open %1").arg(port), 5000);
        }
    });

    // Toolbar Record toggles the RecordingPanel's record/stop. The panel is
    // built later (buildTabs); the lambda dereferences recordingPanel_ only
    // when the user actually toggles, by which time it exists.
    connect(recordAction_, &QAction::toggled, this, [this](bool on) {
        if (!recordingPanel_) return;
        if (on) {
            recordingPanel_->startFromToolbar();
        } else {
            recordingPanel_->stopFromToolbar();
        }
    });

    // Keep the Record action's checked state in sync with the controller's
    // recording state. QSignalBlocker prevents a toggled→start→recordingChanged
    // →setChecked→toggled feedback loop.
    connect(controller_, &SessionController::recordingChanged, this, [this](bool recording) {
        QSignalBlocker blocker(recordAction_);
        recordAction_->setChecked(recording);
    });
}

void MainWindow::buildDock()
{
    auto* dock = new QDockWidget("Acquisition", this);
    dock->setObjectName("configDock");
    dock->setAllowedAreas(Qt::LeftDockWidgetArea);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    // Dock content: Acquisition controls (top) + the Recording module (below,
    // filling the space that used to be empty). Recording was moved out of the
    // tab bar into the sidebar since it is used frequently. Wrapped in a scroll
    // area so the combined height fits a narrow/short dock.
    auto* container = new QWidget(dock);
    auto* vbox      = new QVBoxLayout(container);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(8);

    auto* acqPanel = new AcquisitionPanel(controller_, container);
    acqPanel->setObjectName("acquisitionPanel");
    vbox->addWidget(acqPanel);

    auto* sep = new QFrame(container);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color:#2d333b;");
    vbox->addWidget(sep);

    // Recording module (moved out of the tabs into the left sidebar).
    recordingPanel_ = new RecordingPanel(controller_, container);
    recordingPanel_->setObjectName("recordingPanel");
    vbox->addWidget(recordingPanel_);

    vbox->addStretch(1);

    auto* scroll = new QScrollArea(dock);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(container);
    dock->setWidget(scroll);

    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::buildTabs()
{
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName("centralTabs");
    tabs_->setDocumentMode(true);

    // Monitor tab — real-time 8-channel scrolling traces
    monitorView_ = new MonitorView(controller_, this);
    monitorView_->setObjectName("monitorTab");
    tabs_->addTab(monitorView_, "Monitor");

    // Registers tab — live DeviceConfig editor
    auto* registersView = new RegistersView(controller_, this);
    registersView->setObjectName("registersTab");
    tabs_->addTab(registersView, "Registers");

    // Impedance tab — live per-channel electrode impedance display
    impedanceView_ = new ImpedanceView(controller_, this);
    impedanceView_->setObjectName("impedanceTab");
    tabs_->addTab(impedanceView_, "Impedance");

    // Spectrum tab — live power-spectrum display
    auto* spectrumView = new SpectrumView(controller_, this);
    spectrumView->setObjectName("spectrumTab");
    tabs_->addTab(spectrumView, "Spectrum");

    // (Recording is no longer a tab — it lives in the left "Acquisition" dock,
    //  built in buildDock() which runs before this.)

    // Sessions tab — browse/reveal/export past recordings.
    sessionsView_ = new SessionsView(QString(), this);
    sessionsView_->setObjectName("sessionsTab");
    tabs_->addTab(sessionsView_, "Sessions");

    // Wrap alert bar + tab widget in a container so the alert bar is always
    // visible above all tabs.
    auto* central  = new QWidget(this);
    auto* vLayout  = new QVBoxLayout(central);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->setSpacing(0);

    alertBar_ = new AlertBar(controller_, central);
    alertBar_->setObjectName("alertBar");
    vLayout->addWidget(alertBar_);
    vLayout->addWidget(tabs_);

    setCentralWidget(central);
}

void MainWindow::buildStatusBar()
{
    auto* sb = statusBar();

    // Left: LED + "Link" label
    auto* linkWidget = new QWidget(this);
    auto* linkLayout = new QHBoxLayout(linkWidget);
    linkLayout->setContentsMargins(4, 0, 8, 0);
    linkLayout->setSpacing(4);

    linkLed_ = new LedIndicator(linkWidget);
    linkLed_->setObjectName("linkLed");
    linkLed_->setStatus(LedIndicator::Status::Off);

    auto* linkTextLabel = new QLabel("Link", linkWidget);
    linkTextLabel->setObjectName("linkLabel");

    linkLayout->addWidget(linkLed_);
    linkLayout->addWidget(linkTextLabel);
    sb->addWidget(linkWidget);

    // Monospace metric labels
    const QString monoStyle = "font-family: Menlo, monospace; font-size: 11px;";

    spsLabel_ = new QLabel("SPS: --", this);
    spsLabel_->setObjectName("spsLabel");
    spsLabel_->setStyleSheet(monoStyle);
    sb->addWidget(spsLabel_);

    droppedLabel_ = new QLabel("Dropped: 0", this);
    droppedLabel_->setObjectName("droppedLabel");
    droppedLabel_->setStyleSheet(monoStyle);
    sb->addWidget(droppedLabel_);

    bufferLabel_ = new QLabel("Buffer: 0%", this);
    bufferLabel_->setObjectName("bufferLabel");
    bufferLabel_->setStyleSheet(monoStyle);
    sb->addWidget(bufferLabel_);

    diskLabel_ = new QLabel("Disk: -- GB", this);
    diskLabel_->setObjectName("diskLabel");
    diskLabel_->setStyleSheet(monoStyle);
    sb->addPermanentWidget(diskLabel_);
}

void MainWindow::wireController()
{
    connect(controller_, &SessionController::metricsUpdated,
            this, &MainWindow::onMetricsUpdated);
    connect(controller_, &SessionController::stateChanged,
            this, &MainWindow::onStateChanged);
    connect(controller_, &SessionController::errorOccurred,
            this, &MainWindow::onErrorOccurred);
}

// ──────────────────────────────────────────────────────────────────────────────
// Slots
// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::startStreamingForDemo()
{
    startAction_->setChecked(true);
}

void MainWindow::onStartToggled(bool on)
{
    if (on) {
        controller_->startStreaming();
        linkLed_->setStatus(LedIndicator::Status::Ok);
        startAction_->setText("Stop");
    } else {
        controller_->stopStreaming();
        linkLed_->setStatus(LedIndicator::Status::Off);
        startAction_->setText("Start");
    }
}

void MainWindow::onMetricsUpdated(studio::Metrics m)
{
    spsLabel_->setText(QString("SPS: %1").arg(m.sps, 0, 'f', 0));
    droppedLabel_->setText(QString("Dropped: %1").arg(m.droppedSamples));

    const size_t capacity = controller_->displayBuffer().capacity();
    const int pct = (capacity > 0)
                        ? static_cast<int>(m.bufferFill * 100 / capacity)
                        : 0;
    bufferLabel_->setText(QString("Buffer: %1%").arg(pct));

    if (m.droppedSamples > 0) {
        droppedLabel_->setStyleSheet("color:#f85149; font-family: Menlo, monospace; font-size: 11px;");
    } else {
        droppedLabel_->setStyleSheet("font-family: Menlo, monospace; font-size: 11px;");
    }
}

void MainWindow::onStateChanged(studio::State s)
{
    switch (s) {
    case State::Streaming:
    case State::Recording:
        linkLed_->setStatus(LedIndicator::Status::Ok);
        break;
    case State::Idle:
        linkLed_->setStatus(LedIndicator::Status::Off);
        break;
    }
}

void MainWindow::onErrorOccurred(const QString& message)
{
    linkLed_->setStatus(LedIndicator::Status::Error);
    statusBar()->showMessage(message, 5000);
}

void MainWindow::refreshDiskLabel()
{
    const qint64 bytes = QStorageInfo(QDir::currentPath()).bytesAvailable();
    if (bytes >= 0) {
        const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
        diskLabel_->setText(QString("Disk: %1 GB").arg(gb, 0, 'f', 1));
    } else {
        diskLabel_->setText("Disk: -- GB");
    }
}

} // namespace studio
