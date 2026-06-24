// src/ui/ShortcutsHelpDialog.cpp
//
// See ShortcutsHelpDialog.h for public contract.

#include "ui/ShortcutsHelpDialog.h"
#include "ui/Shortcuts.h"
#include "ui/theme/Theme.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace studio {

ShortcutsHelpDialog::ShortcutsHelpDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Keyboard Shortcuts");
    setModal(true);
    setMinimumWidth(420);

    // ── Outer layout ─────────────────────────────────────────────────────────
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(20, 16, 20, 16);
    outerLayout->setSpacing(12);

    // ── Header label ─────────────────────────────────────────────────────────
    auto* header = new QLabel("Keyboard Shortcuts", this);
    header->setStyleSheet(
        QString("font-size: 15px; font-weight: bold; color: %1;")
            .arg(theme::kTextPrimary));
    outerLayout->addWidget(header);

    // ── Two-column grid ───────────────────────────────────────────────────────
    auto* grid = new QGridLayout();
    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 1);
    grid->setHorizontalSpacing(20);
    grid->setVerticalSpacing(6);

    // Column headers
    auto* hdrKey = new QLabel("Shortcut", this);
    hdrKey->setStyleSheet(
        QString("color: %1; font-size: 11px; font-weight: bold;")
            .arg(theme::kTextMuted));
    auto* hdrAction = new QLabel("Action", this);
    hdrAction->setStyleSheet(
        QString("color: %1; font-size: 11px; font-weight: bold;")
            .arg(theme::kTextMuted));
    grid->addWidget(hdrKey,    0, 0);
    grid->addWidget(hdrAction, 0, 1);

    // Shortcut rows
    const QString keyStyle = QString(
        "font-family: %1; font-size: 12px; color: %2;"
        "background: %3; border: 1px solid %4;"
        "border-radius: 4px; padding: 1px 6px;")
        .arg(theme::kMonoFamily)
        .arg(theme::kTextPrimary)
        .arg(theme::kSurfaceAlt)
        .arg(theme::kBorder);

    const QString actionStyle = QString("color: %1; font-size: 12px;")
        .arg(theme::kTextPrimary);

    int row = 1;
    for (const ShortcutDef& def : shortcutDefs()) {
        auto* keyLabel = new QLabel(def.keys, this);
        keyLabel->setStyleSheet(keyStyle);
        keyLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        auto* actionLabel = new QLabel(def.action, this);
        actionLabel->setStyleSheet(actionStyle);

        grid->addWidget(keyLabel,    row, 0);
        grid->addWidget(actionLabel, row, 1);
        ++row;
    }

    outerLayout->addLayout(grid);

    // ── macOS note ───────────────────────────────────────────────────────────
    auto* note = new QLabel("On macOS, Ctrl shortcuts use the ⌘ (Command) key.", this);
    note->setStyleSheet(
        QString("color: %1; font-size: 10px; font-style: italic;")
            .arg(theme::kTextMuted));
    outerLayout->addWidget(note);

    // ── Close button ─────────────────────────────────────────────────────────
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::accept);
    outerLayout->addWidget(buttonBox);

    // ── Dark theme background ─────────────────────────────────────────────────
    setStyleSheet(
        QString("QDialog { background: %1; } QLabel { color: %2; }")
            .arg(theme::kSurface)
            .arg(theme::kTextPrimary));
}

} // namespace studio
