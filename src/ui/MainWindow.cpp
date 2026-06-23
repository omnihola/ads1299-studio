// src/ui/MainWindow.cpp
//
// See MainWindow.h for public contract.

#include "ui/MainWindow.h"

#include <QAction>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QWidget>
#include <QDir>
#include <QStorageInfo>

#include "app/SessionController.h"
#include "core/acquisition/SimulatedSource.h"

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
}

MainWindow::~MainWindow() = default;

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
    startAction_->setToolTip("Start / stop streaming");

    recordAction_ = new QAction("Record", this);
    recordAction_->setCheckable(true);
    recordAction_->setToolTip("Start / stop recording");

    toolbar->addAction(connectAction_);
    toolbar->addSeparator();
    toolbar->addAction(startAction_);
    toolbar->addAction(recordAction_);

    connect(startAction_, &QAction::toggled, this, &MainWindow::onStartToggled);
}

void MainWindow::buildDock()
{
    auto* dock = new QDockWidget("Configuration", this);
    dock->setObjectName("configDock");
    dock->setAllowedAreas(Qt::LeftDockWidgetArea);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    // Placeholder — Task 17 will replace this with the real config panel
    auto* placeholder = new QWidget(dock);
    placeholder->setObjectName("configPlaceholder");
    dock->setWidget(placeholder);

    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::buildTabs()
{
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName("centralTabs");
    tabs_->setDocumentMode(true);

    const QStringList tabNames = { "Monitor", "Registers", "Impedance", "Spectrum", "Recording" };
    for (const auto& name : tabNames) {
        auto* placeholder = new QWidget();
        placeholder->setObjectName(name.toLower() + "Tab");
        tabs_->addTab(placeholder, name);
    }

    setCentralWidget(tabs_);
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
    const QString monoStyle = "font-family: 'SF Mono', 'JetBrains Mono', Menlo, monospace; font-size: 11px;";

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
        droppedLabel_->setStyleSheet("color:#f85149; font-family: 'SF Mono', 'JetBrains Mono', Menlo, monospace; font-size: 11px;");
    } else {
        droppedLabel_->setStyleSheet("font-family: 'SF Mono', 'JetBrains Mono', Menlo, monospace; font-size: 11px;");
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
