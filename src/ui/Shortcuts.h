#pragma once
// src/ui/Shortcuts.h
//
// Single source of truth for keyboard shortcut definitions.
// Used by MainWindow (to wire QShortcuts/QActions) and ShortcutsHelpDialog
// (to populate the help table), and by unit tests.
//
// On macOS, Qt automatically maps "Ctrl" → Cmd in QKeySequence, which
// provides the expected Mac-native modifier behaviour.

#include <QVector>

namespace studio {

struct ShortcutDef {
    const char* keys;
    const char* action;
};

inline QVector<ShortcutDef> shortcutDefs()
{
    return {
        {"F5",      "Start / Stop streaming"},
        {"Ctrl+R",  "Start / Stop recording"},
        {"Ctrl+M",  "Add event marker (while recording)"},
        {"Ctrl+P",  "Pause / resume monitor display"},
        {"Ctrl+1",  "Monitor tab"},
        {"Ctrl+2",  "Registers tab"},
        {"Ctrl+3",  "Impedance tab"},
        {"Ctrl+4",  "Spectrum tab"},
        {"Ctrl+5",  "Recording tab"},
        {"F1",      "Keyboard shortcuts help"},
    };
}

} // namespace studio
