#pragma once
// src/ui/ShortcutsHelpDialog.h
//
// ShortcutsHelpDialog — modal dialog showing all keyboard shortcuts.
// Populated from shortcutDefs() in Shortcuts.h (single source of truth).

#include <QDialog>

namespace studio {

class ShortcutsHelpDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ShortcutsHelpDialog(QWidget* parent = nullptr);
    ~ShortcutsHelpDialog() override = default;
};

} // namespace studio
