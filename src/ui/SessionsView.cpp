// src/ui/SessionsView.cpp
//
// See SessionsView.h for the public contract.

#include "ui/SessionsView.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include "core/recording/SessionExporter.h"
#include "core/recording/SessionScanner.h"
#include "ui/theme/Theme.h"

namespace studio {

namespace {

// Column indices
constexpr int kColSubject  = 0;
constexpr int kColDate     = 1;
constexpr int kColDuration = 2;
constexpr int kColRate     = 3;
constexpr int kColSamples  = 4;
constexpr int kColBdf      = 5;
constexpr int kColCsv      = 6;
constexpr int kNumCols     = 7;

// Role for storing the basePath of each row
constexpr int kBasePathRole = Qt::UserRole + 1;

QString formatDuration(double secs)
{
    if (secs < 0.0) secs = 0.0;
    const int totalSec = static_cast<int>(secs);
    const int minutes  = totalSec / 60;
    const int seconds  = totalSec % 60;
    return QString("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QTableWidgetItem* monoItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setFont(QFont("Menlo", 11));
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    return item;
}

QTableWidgetItem* textItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    return item;
}

} // anonymous namespace

// ── Construction ─────────────────────────────────────────────────────────────

SessionsView::SessionsView(const QString& defaultDir, QWidget* parent)
    : QWidget(parent)
{
    buildUi(defaultDir);
}

// ── buildUi ──────────────────────────────────────────────────────────────────

void SessionsView::buildUi(const QString& defaultDir)
{
    setStyleSheet(QString(
        "SessionsView { background: %1; }"
        "QTableWidget { background: %2; color: %3; "
        "  gridline-color: %4; border: 1px solid %4; }"
        "QTableWidget::item:selected { background: %5; color: %3; }"
        "QHeaderView::section { background: %6; color: %3; "
        "  border: 1px solid %4; padding: 4px; }"
        "QLineEdit { background: %2; color: %3; border: 1px solid %4; "
        "  border-radius: 4px; padding: 4px; }"
        "QPushButton { background: %6; color: %3; border: 1px solid %4; "
        "  border-radius: 4px; padding: 4px 10px; }"
        "QPushButton:hover { background: %7; }"
        "QPushButton:disabled { color: %8; }")
        .arg(theme::kBg)
        .arg(theme::kSurface)
        .arg(theme::kTextPrimary)
        .arg(theme::kBorder)
        .arg(theme::kAccent)
        .arg(theme::kSurfaceAlt)
        .arg(theme::kAccentHover)
        .arg(theme::kTextMuted));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    // ── Directory row ─────────────────────────────────────────────────────────
    auto* dirRow = new QHBoxLayout;
    auto* dirLabel = new QLabel("Recordings folder:", this);
    dirLabel->setStyleSheet(QString("color: %1;").arg(theme::kTextMuted));

    dirEdit_   = new QLineEdit(defaultDir.isEmpty() ? QDir::currentPath() : defaultDir, this);
    browseBtn_ = new QPushButton("Browse…", this);
    refreshBtn_= new QPushButton("Refresh", this);

    dirRow->addWidget(dirLabel);
    dirRow->addWidget(dirEdit_, 1);
    dirRow->addWidget(browseBtn_);
    dirRow->addWidget(refreshBtn_);
    root->addLayout(dirRow);

    // ── Table ─────────────────────────────────────────────────────────────────
    table_ = new QTableWidget(0, kNumCols, this);
    table_->setHorizontalHeaderLabels(
        {"Subject", "Date (UTC)", "Duration", "Rate (Hz)", "Samples", "BDF", "CSV"});
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(kColSubject, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(kColDate,     QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(kColDuration, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(kColRate,     QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(kColSamples,  QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(kColBdf,      QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(kColCsv,      QHeaderView::ResizeToContents);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setVisible(false);
    root->addWidget(table_, 1);

    // ── Action buttons ────────────────────────────────────────────────────────
    auto* actionRow = new QHBoxLayout;
    revealBtn_ = new QPushButton("Reveal in Finder", this);
    exportBtn_ = new QPushButton("Export…",          this);
    revealBtn_->setEnabled(false);
    exportBtn_->setEnabled(false);
    actionRow->addStretch();
    actionRow->addWidget(revealBtn_);
    actionRow->addWidget(exportBtn_);
    root->addLayout(actionRow);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(browseBtn_,  &QPushButton::clicked,
            this, &SessionsView::onBrowse);
    connect(refreshBtn_, &QPushButton::clicked,
            this, &SessionsView::onRefresh);
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SessionsView::onSelectionChanged);
    connect(revealBtn_, &QPushButton::clicked,
            this, &SessionsView::onReveal);
    connect(exportBtn_, &QPushButton::clicked,
            this, &SessionsView::onExport);

    // Initial populate
    populateTable();
}

// ── populateTable ─────────────────────────────────────────────────────────────

void SessionsView::populateTable()
{
    table_->setRowCount(0);

    const QString dir = dirEdit_->text().trimmed();
    if (dir.isEmpty()) return;

    const QVector<SessionInfo> sessions = SessionScanner::scan(dir);

    table_->setRowCount(sessions.size());
    for (int row = 0; row < sessions.size(); ++row) {
        const SessionInfo& s = sessions[row];

        auto* subjectItem = textItem(s.subjectId.isEmpty() ? "(no subject)" : s.subjectId);
        subjectItem->setData(kBasePathRole, s.basePath);
        table_->setItem(row, kColSubject,  subjectItem);
        table_->setItem(row, kColDate,     textItem(s.startTimeUtc));
        table_->setItem(row, kColDuration, monoItem(formatDuration(s.durationSec)));
        table_->setItem(row, kColRate,     monoItem(QString::number(s.sampleRate)));
        table_->setItem(row, kColSamples,  monoItem(QString::number(s.totalSamples)));
        table_->setItem(row, kColBdf,      textItem(s.hasBdf ? "✓" : "–"));
        table_->setItem(row, kColCsv,      textItem(s.hasCsv ? "✓" : "–"));
    }

    revealBtn_->setEnabled(false);
    exportBtn_->setEnabled(false);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void SessionsView::onBrowse()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, "Select recordings folder", dirEdit_->text());
    if (!chosen.isEmpty()) {
        dirEdit_->setText(chosen);
        populateTable();
    }
}

void SessionsView::onRefresh()
{
    populateTable();
}

void SessionsView::onSelectionChanged()
{
    const bool hasSelection = !table_->selectedItems().isEmpty();
    revealBtn_->setEnabled(hasSelection);
    exportBtn_->setEnabled(hasSelection);
}

void SessionsView::onReveal()
{
    const int row = table_->currentRow();
    if (row < 0) return;

    const auto* item = table_->item(row, kColSubject);
    if (!item) return;

    const QString basePath = item->data(kBasePathRole).toString();
    const QString dir = QFileInfo(basePath).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void SessionsView::onExport()
{
    const int row = table_->currentRow();
    if (row < 0) return;

    const auto* item = table_->item(row, kColSubject);
    if (!item) return;

    const QString basePath = item->data(kBasePathRole).toString();

    const QString dest = QFileDialog::getExistingDirectory(
        this, "Export session to…", QDir::homePath());
    if (dest.isEmpty()) return;

    QStringList copied;
    QString     error;
    const bool  ok = SessionExporter::exportTo(basePath, dest, copied, error);

    if (ok) {
        QMessageBox::information(
            this,
            "Export complete",
            QString("Exported %1 file(s) to:\n%2").arg(copied.size()).arg(dest));
    } else {
        QMessageBox::warning(
            this,
            "Export failed",
            QString("Export failed:\n%1").arg(error));
    }
}

} // namespace studio
