// src/ui/RecordingPanel.cpp
//
// See RecordingPanel.h for the public contract.

#include "ui/RecordingPanel.h"

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
#include <QTimer>
#include <QVBoxLayout>

#include "app/AppState.h"
#include "app/SessionController.h"
#include "core/recording/SessionMetadata.h"

namespace studio {

namespace {
constexpr int kSampleRate    = 250;   // default sample rate (display + meta)
constexpr int kReadoutMs     = 500;   // live-readout refresh interval
} // namespace

// Elapsed timer used for the live mm:ss readout. Kept file-local (not in the
// header) to avoid leaking <QElapsedTimer> into every includer.
static QElapsedTimer s_recordElapsed;

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
    browseButton_ = new QPushButton("Browse…", this);
    browseButton_->setObjectName("browse");
    browseButton_->setFixedWidth(84);
    folderRow->addWidget(folderEdit_);
    folderRow->addWidget(browseButton_);
    form->addRow("Output Folder:", folderRow);

    auto* srLabel = new QLabel(QString("%1 Hz").arg(kSampleRate), this);
    srLabel->setObjectName("sampleRateLabel");
    form->addRow("Sample Rate:", srLabel);

    mainLayout->addLayout(form);

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

    // ── Live readout ──
    readoutLabel_ = new QLabel("--:-- | 0 samples", this);
    readoutLabel_->setObjectName("readoutLabel");
    readoutLabel_->setStyleSheet(
        "font-family: 'SF Mono', 'JetBrains Mono', Menlo, monospace; font-size: 12px;");
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
    connect(readoutTimer_,    &QTimer::timeout,      this, &RecordingPanel::updateReadout);
    connect(controller_,      &SessionController::recordingChanged,
            this,             &RecordingPanel::onRecordingChanged);
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
    const QFileInfo fi(folder);
    if (folder.isEmpty() || !fi.isDir() || !fi.isWritable()) {
        QMessageBox::warning(this, "Validation Error",
                             "Output folder must exist and be writable.");
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
    const QString ts        = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString basePath  = folder + "/" + subjectId + "_" + ts;

    const auto meta = SessionMetadata()
        .withSubjectId(subjectId)
        .withMontage(montageEdit_->text().trimmed())
        .withNotes(notesEdit_->toPlainText())
        .withSampleRate(kSampleRate)
        .withStartTimeUtc(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    if (!controller_->startRecording(basePath, meta)) {
        QMessageBox::critical(this, "Recording Error",
                              "Failed to start recording. Streaming must be active "
                              "and the output folder writable.");
        return;
    }
    s_recordElapsed.restart();
    // Button states are updated by onRecordingChanged via the controller signal.
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

    if (recording) {
        s_recordElapsed.restart();
        readoutTimer_->start();
        updateReadout();
    } else {
        readoutTimer_->stop();
        readoutLabel_->setText("--:-- | 0 samples");
    }
}

void RecordingPanel::updateReadout()
{
    const qint64 totalSec = s_recordElapsed.isValid() ? s_recordElapsed.elapsed() / 1000 : 0;
    const int minutes = static_cast<int>(totalSec / 60);
    const int seconds = static_cast<int>(totalSec % 60);
    const quint64 samples = controller_->recordedSamples();

    readoutLabel_->setText(
        QString("%1:%2 | %3 samples")
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'))
            .arg(samples));
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
