#pragma once
// src/ui/SessionsView.h
//
// SessionsView — a widget that lists past recordings in a table and provides
// "Reveal in Finder" and "Export…" actions for the selected row.

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace studio {

class SessionsView : public QWidget
{
    Q_OBJECT

public:
    explicit SessionsView(const QString& defaultDir = QString(),
                          QWidget* parent = nullptr);

private slots:
    void onBrowse();
    void onRefresh();
    void onSelectionChanged();
    void onReveal();
    void onExport();

private:
    void buildUi(const QString& defaultDir);
    void populateTable();

    // ── widgets ───────────────────────────────────────────────────────────────
    QLineEdit*    dirEdit_     = nullptr;
    QPushButton*  browseBtn_   = nullptr;
    QPushButton*  refreshBtn_  = nullptr;
    QTableWidget* table_       = nullptr;
    QPushButton*  revealBtn_   = nullptr;
    QPushButton*  exportBtn_   = nullptr;
};

} // namespace studio
