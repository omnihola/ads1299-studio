#pragma once
// src/ui/PreRecordCheckDialog.h
//
// Pre-recording signal-quality check gate.  Shown before each recording
// starts to let the operator review per-channel lead-off status and
// choose to proceed or cancel.

#include <QDialog>
#include <QString>
#include <cstdint>

namespace studio {

class PreRecordCheckDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreRecordCheckDialog(uint8_t leadOffP, uint8_t leadOffN,
                                  QWidget* parent = nullptr);

    // ── Pure static helpers (testable without widgets) ────────────────────
    // Returns count of channels c in 0..7 where NEITHER leadOffP nor
    // leadOffN bit c is set (i.e. the electrode is properly connected).
    static int okChannelCount(uint8_t leadOffP, uint8_t leadOffN);

    // Human-readable summary, e.g.:
    //   "6 of 8 electrodes connected — CH3, CH6 lead-off"
    //   "8 of 8 electrodes connected"
    static QString channelStatusText(uint8_t leadOffP, uint8_t leadOffN);
};

} // namespace studio
