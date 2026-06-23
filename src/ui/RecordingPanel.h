#pragma once
// src/ui/RecordingPanel.h
//
// RecordingPanel — form widget for capturing session metadata and driving the
// recording lifecycle via SessionController. Validates inputs at the UI
// boundary (non-empty subject ID, writable output folder) before starting.

#include <QWidget>

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

    // Live readout
    QLabel*         readoutLabel_    = nullptr;
    QTimer*         readoutTimer_    = nullptr;
};

} // namespace studio
