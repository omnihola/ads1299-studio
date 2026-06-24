#pragma once
// src/ui/RecordingPanel.h
//
// RecordingPanel — form widget for capturing session metadata and driving the
// recording lifecycle via SessionController. Validates inputs at the UI
// boundary (non-empty subject ID, writable output folder) before starting.

#include <QWidget>
#include <QElapsedTimer>
#include <cstdint>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QTimer;

namespace studio {

class SessionController;

class RecordingPanel : public QWidget
{
    Q_OBJECT

public:
    explicit RecordingPanel(SessionController* controller,
                            QWidget* parent = nullptr);

public slots:
    // Invoked by the toolbar Record action when toggled on/off.
    void startFromToolbar();
    void stopFromToolbar();

private slots:
    void onRecordClicked();
    void onStopClicked();
    void onAddMarkerClicked();
    void onBrowseClicked();
    void onExportClicked();
    void onRecordingChanged(bool recording);
    void updateReadout();

private:
    void buildUi();
    void wireSignals();
    bool validateInputs();          // shows inline warning + returns false on failure
    void setRecordingMode(bool recording);

    SessionController* controller_ = nullptr;

    // Form fields
    QLineEdit*      subjectIdEdit_   = nullptr;
    QLineEdit*      montageEdit_     = nullptr;
    QPlainTextEdit* notesEdit_       = nullptr;
    QLineEdit*      folderEdit_      = nullptr;
    QPushButton*    browseButton_    = nullptr;

    // Control buttons
    QPushButton*    recordButton_    = nullptr;
    QPushButton*    stopButton_      = nullptr;
    QPushButton*    addMarkerButton_ = nullptr;
    QLineEdit*      markerLabelEdit_ = nullptr;
    QPushButton*    exportButton_    = nullptr;

    // Tracks the base path of the most recently started recording (persists after stop).
    QString         lastBasePath_;

    // Live readout
    QLabel*         readoutLabel_    = nullptr;
    QLabel*         sampleRateLabel_ = nullptr;  // FIX 4: shows actual config sample rate
    QTimer*         readoutTimer_    = nullptr;
    QElapsedTimer   recordElapsed_;   // restarted on record; drives mm:ss readout

    // Signal-quality gate
    QCheckBox*      skipQualityCheckBox_ = nullptr;
    uint8_t         lastLeadOffP_        = 0;
    uint8_t         lastLeadOffN_        = 0;
};

} // namespace studio
