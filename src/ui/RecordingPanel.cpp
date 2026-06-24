// src/ui/RecordingPanel.cpp
//
// See RecordingPanel.h for the public contract.

#include "ui/RecordingPanel.h"
#include "ui/PreRecordCheckDialog.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>

#include "app/SettingsKeys.h"
#include <QTimer>
#include <QVBoxLayout>

#include "app/AppState.h"
#include "app/SessionController.h"
#include "core/recording/SessionExporter.h"
#include "core/recording/SessionMetadata.h"

namespace studio {

namespace {
constexpr int kReadoutMs     = 500;   // live-readout refresh interval
} // namespace

RecordingPanel::RecordingPanel(SessionController* controller, QWidget* parent)
    : QWidget(parent)
    , controller_(controller)
{
    buildUi();
    wireSignals();
}

// ─── UI construction ─────────────────────────────────────────────────────────

void RecordingPanel::buildUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setSpacing(6);

    subjectIdEdit_ = new QLineEdit(this);
    subjectIdEdit_->setObjectName("subjectId");
    subjectIdEdit_->setPlaceholderText("e.g. P001");
    form->addRow("Subject ID:", subjectIdEdit_);

    montageEdit_ = new QLineEdit("10-20", this);
    montageEdit_->setObjectName("montage");
    form->addRow("Montage:", montageEdit_);

    notesEdit_ = new QPlainTextEdit(this);
    notesEdit_->setObjectName("notes");
    notesEdit_->setMaximumHeight(80);
    notesEdit_->setPlaceholderText("Operator notes…");
    form->addRow("Notes:", notesEdit_);

    auto* folderRow = new QHBoxLayout;
    folderEdit_ = new QLineEdit(this);
    folderEdit_->setObjectName("outputFolder");
    folderEdit_->setPlaceholderText("/path/to/recordings");
    // Pre-fill a real, writable default folder so recording works out of the box
    // (the user can still change it via Browse). The folder itself is created
    // lazily at record time (validateInputs), so launching the app doesn't litter
    // the filesystem if recording is never used.
    {
        // Prefer the folder the user last used (persisted), else a sensible default.
        const QString saved = QSettings().value(settings::kRecordingOutputFolder).toString();
        const QString defaultDir =
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + "/ADS1299 Recordings";
        const QString initial = saved.isEmpty() ? defaultDir : saved;
        if (!initial.isEmpty())
            folderEdit_->setText(initial);
    }
    browseButton_ = new QPushButton("Browse…", this);
    browseButton_->setObjectName("browse");
    browseButton_->setFixedWidth(84);
    folderRow->addWidget(folderEdit_);
    folderRow->addWidget(browseButton_);
    form->addRow("Output Folder:", folderRow);

    // FIX 4: Show the actual configured sample rate, not a hardcoded constant.
    const int initialSr = controller_ ? controller_->config().sampleRate() : 250;
    sampleRateLabel_ = new QLabel(QString("%1 Hz").arg(initialSr), this);
    sampleRateLabel_->setObjectName("sampleRateLabel");
    form->addRow("Sample Rate:", sampleRateLabel_);

    mainLayout->addLayout(form);

    // ── Signal-quality gate option ──
    skipQualityCheckBox_ = new QCheckBox("Skip pre-recording signal check", this);
    skipQualityCheckBox_->setObjectName("skipQualityCheck");
    skipQualityCheckBox_->setChecked(false);
    mainLayout->addWidget(skipQualityCheckBox_);

    writeCsvCheckBox_ = new QCheckBox("Write per-sample CSV (large at high rates)", this);
    writeCsvCheckBox_->setObjectName("writeCsv");
    // Remember the user's last CSV choice across launches (defaults to on).
    writeCsvCheckBox_->setChecked(
        QSettings().value(settings::kRecordingWriteCsv, true).toBool());
    connect(writeCsvCheckBox_, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(settings::kRecordingWriteCsv, on);
    });
    writeCsvCheckBox_->setToolTip(
        "Also write the full per-sample CSV alongside the lossless BDF. "
        "Uncheck for long/high-rate sessions to save disk and CPU (BDF is unaffected).");
    mainLayout->addWidget(writeCsvCheckBox_);

    // ── Control buttons ──
    auto* controls = new QHBoxLayout;

    recordButton_ = new QPushButton("Record", this);
    recordButton_->setObjectName("record");

    stopButton_ = new QPushButton("Stop", this);
    stopButton_->setObjectName("stop");
    stopButton_->setEnabled(false);

    addMarkerButton_ = new QPushButton("Add Marker", this);
    addMarkerButton_->setObjectName("addMarker");
    addMarkerButton_->setEnabled(false);

    markerLabelEdit_ = new QLineEdit("marker", this);
    markerLabelEdit_->setObjectName("markerLabel");
    markerLabelEdit_->setMaximumWidth(120);
    markerLabelEdit_->setEnabled(false);

    controls->addWidget(recordButton_);
    controls->addWidget(stopButton_);
    controls->addWidget(addMarkerButton_);
    controls->addWidget(markerLabelEdit_);
    controls->addStretch();
    mainLayout->addLayout(controls);

    // ── Export button ──
    exportButton_ = new QPushButton("Export session…", this);
    exportButton_->setObjectName("exportSession");
    exportButton_->setEnabled(false);  // enabled only after a session has been recorded
    exportButton_->setToolTip("Copy session files (.bdf, .csv, .meta.json) to a chosen folder");
    mainLayout->addWidget(exportButton_);

    // ── Live readout ──
    readoutLabel_ = new QLabel("--:-- | 0 samples", this);
    readoutLabel_->setObjectName("readoutLabel");
    readoutLabel_->setStyleSheet(
        "font-family: Menlo, monospace; font-size: 12px;");
    mainLayout->addWidget(readoutLabel_);

    mainLayout->addStretch();

    readoutTimer_ = new QTimer(this);
    readoutTimer_->setInterval(kReadoutMs);
}

void RecordingPanel::wireSignals()
{
    connect(recordButton_,    &QPushButton::clicked, this, &RecordingPanel::onRecordClicked);
    connect(stopButton_,      &QPushButton::clicked, this, &RecordingPanel::onStopClicked);
    connect(addMarkerButton_, &QPushButton::clicked, this, &RecordingPanel::onAddMarkerClicked);
    connect(browseButton_,    &QPushButton::clicked, this, &RecordingPanel::onBrowseClicked);
    connect(exportButton_,    &QPushButton::clicked, this, &RecordingPanel::onExportClicked);
    connect(readoutTimer_,    &QTimer::timeout,      this, &RecordingPanel::updateReadout);
    connect(controller_,      &SessionController::recordingChanged,
            this,             &RecordingPanel::onRecordingChanged);
    // FIX 4: Keep sample-rate label honest when config changes.
    connect(controller_, &SessionController::configChanged,
            this, [this](const studio::DeviceConfig& cfg) {
        sampleRateLabel_->setText(QString("%1 Hz").arg(cfg.sampleRate()));
    });
    // Cache latest lead-off bits for the pre-recording quality gate.
    connect(controller_, &SessionController::metricsUpdated,
            this, [this](const studio::Metrics& m) {
        lastLeadOffP_ = m.leadOffP;
        lastLeadOffN_ = m.leadOffN;
    });
}

// ─── Validation ──────────────────────────────────────────────────────────────

bool RecordingPanel::validateInputs()
{
    if (subjectIdEdit_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Validation Error",
                             "Subject ID must not be empty.");
        subjectIdEdit_->setFocus();
        return false;
    }

    const QString folder = folderEdit_->text().trimmed();
    if (folder.isEmpty()) {
        QMessageBox::warning(this, "Validation Error", "Output folder must be set.");
        folderEdit_->setFocus();
        return false;
    }
    // Create the folder if it doesn't exist yet (the pre-filled default, or any new
    // path the user typed) — standard recording-app behavior, and avoids creating it
    // on launch when recording is never used.
    QDir().mkpath(folder);
    const QFileInfo fi(folder);
    if (!fi.isDir() || !fi.isWritable()) {
        QMessageBox::warning(this, "Validation Error",
                             "Output folder could not be created or is not writable.");
        folderEdit_->setFocus();
        return false;
    }

    return true;
}

// ─── Slots ───────────────────────────────────────────────────────────────────

void RecordingPanel::onRecordClicked()
{
    if (!validateInputs()) return;

    const QString subjectId = subjectIdEdit_->text().trimmed();
    const QString folder    = folderEdit_->text().trimmed();
    // Remember this (validated) folder so the next launch defaults to it.
    QSettings().setValue(settings::kRecordingOutputFolder, folder);

    // ── Low-disk-space guard ─────────────────────────────────────────────────
    // High sample rates fill a volume fast (BDF + per-sample CSV); a disk that
    // fills mid-session loses data. Warn (but let the operator proceed) when the
    // output volume is low on space.
    {
        constexpr qint64 kLowSpaceBytes = 1024LL * 1024 * 1024;  // 1 GB
        const QStorageInfo storage(folder);
        if (storage.isValid() && storage.isReady()
            && storage.bytesAvailable() < kLowSpaceBytes) {
            const qint64 freeMb = storage.bytesAvailable() / (1024 * 1024);
            const auto ret = QMessageBox::warning(
                this, "Low Disk Space",
                QString("Only %1 MB free on the output volume. Recording can fill "
                        "this quickly at high sample rates. Continue anyway?").arg(freeMb),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret != QMessageBox::Yes) return;
        }
    }

    // ── Signal-quality gate ──────────────────────────────────────────────────
    // Show the per-channel lead-off dialog unless the operator opted out.
    // The headless path (controller_->startRecording called directly in tests)
    // bypasses this gate entirely — it lives only in the UI flow.
    // Shown BEFORE stamping the start time so the recorded start time and the
    // filename reflect the actual recording start, not the button click (the
    // operator may sit in this dialog for a while).
    if (!skipQualityCheckBox_->isChecked()) {
        PreRecordCheckDialog dlg(lastLeadOffP_, lastLeadOffN_, this);
        if (dlg.exec() != QDialog::Accepted) return;
    }

    // Stamp the start instant now (post-dialog); use it for both the filename
    // (local time) and the metadata start time (UTC) so they stay consistent.
    const QDateTime now     = QDateTime::currentDateTime();
    const QString   ts      = now.toString("yyyyMMdd-HHmmss");
    const QString   basePath = folder + "/" + subjectId + "_" + ts;

    // FIX 4: pass the actual configured sample rate; SessionController::startRecording
    // will override it anyway via withSampleRate(config_.sampleRate()), but keeping
    // the panel honest avoids confusion in logging.
    const int sr = controller_->config().sampleRate();
    const auto meta = SessionMetadata()
        .withSubjectId(subjectId)
        .withMontage(montageEdit_->text().trimmed())
        .withNotes(notesEdit_->toPlainText())
        .withSampleRate(sr)
        .withStartTimeUtc(now.toUTC().toString(Qt::ISODateWithMs));

    if (!controller_->startRecording(basePath, meta, writeCsvCheckBox_->isChecked())) {
        QMessageBox::critical(this, "Recording Error",
                              "Failed to start recording. Streaming must be active "
                              "and the output folder writable.");
        return;
    }
    lastBasePath_ = basePath;
    // Button states AND the elapsed-time clock are set by setRecordingMode(), invoked
    // via the controller's recordingChanged signal — so we don't restart the clock here
    // (the toolbar Record path relies on that same handler too).
}

void RecordingPanel::onStopClicked()
{
    controller_->stopRecording();
}

void RecordingPanel::onAddMarkerClicked()
{
    const QString label = markerLabelEdit_->text().trimmed();
    controller_->addMarker(label.isEmpty() ? QStringLiteral("marker") : label);
}

void RecordingPanel::onBrowseClicked()
{
    const QString start = folderEdit_->text().trimmed().isEmpty()
                              ? QDir::homePath()
                              : folderEdit_->text().trimmed();
    const QString dir = QFileDialog::getExistingDirectory(this, "Select Output Folder", start);
    if (!dir.isEmpty()) {
        folderEdit_->setText(dir);
        // Persist the explicit user choice immediately.
        QSettings().setValue(settings::kRecordingOutputFolder, dir);
    }
}

void RecordingPanel::onRecordingChanged(bool recording)
{
    setRecordingMode(recording);
}

void RecordingPanel::setRecordingMode(bool recording)
{
    recordButton_->setEnabled(!recording);
    stopButton_->setEnabled(recording);
    addMarkerButton_->setEnabled(recording);
    markerLabelEdit_->setEnabled(recording);

    subjectIdEdit_->setEnabled(!recording);
    montageEdit_->setEnabled(!recording);
    notesEdit_->setEnabled(!recording);
    folderEdit_->setEnabled(!recording);
    browseButton_->setEnabled(!recording);

    // Export is available only when there is a completed session and we are not recording.
    exportButton_->setEnabled(!recording && !lastBasePath_.isEmpty());

    if (recording) {
        recordElapsed_.restart();
        readoutTimer_->start();
        updateReadout();
    } else {
        readoutTimer_->stop();
        // Keep the final readout visible (don't blank it) so the operator can see
        // what was just captured. It refreshes when the next recording starts.
    }
}

void RecordingPanel::updateReadout()
{
    const qint64 totalSec = recordElapsed_.isValid() ? recordElapsed_.elapsed() / 1000 : 0;
    const int minutes = static_cast<int>(totalSec / 60);
    const int seconds = static_cast<int>(totalSec % 60);
    const quint64 samples = controller_->recordedSamples();
    const int     markers = controller_->markerCount();

    readoutLabel_->setText(
        QString("%1:%2 | %3 samples | %4 markers")
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'))
            .arg(samples)
            .arg(markers));
}

void RecordingPanel::onExportClicked()
{
    if (lastBasePath_.isEmpty()) return;

    const QString dest = QFileDialog::getExistingDirectory(
        this, "Export session to folder…");
    if (dest.isEmpty()) return;

    QStringList copied;
    QString error;
    const bool ok = SessionExporter::exportTo(lastBasePath_, dest, copied, error);

    if (ok) {
        QMessageBox::information(
            this, "Export Complete",
            QString("Exported %1 file(s) to %2.").arg(copied.size()).arg(dest));
    } else {
        QMessageBox::warning(
            this, "Export Failed",
            error);
    }
}

// ─── Toolbar entry points ────────────────────────────────────────────────────

void RecordingPanel::startFromToolbar()
{
    onRecordClicked();
}

void RecordingPanel::stopFromToolbar()
{
    if (controller_->state() == State::Recording) {
        controller_->stopRecording();
    }
}

} // namespace studio
