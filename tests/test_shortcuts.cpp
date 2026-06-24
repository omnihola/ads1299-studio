// tests/test_shortcuts.cpp
//
// QtTest unit tests for keyboard shortcut definitions and ShortcutsHelpDialog.
// Run headless: QT_QPA_PLATFORM=offscreen

#include <QSet>
#include <QString>
#include <QtTest/QtTest>
#include <QApplication>

#include "ui/Shortcuts.h"
#include "ui/ShortcutsHelpDialog.h"

namespace studio {

class TestShortcuts : public QObject
{
    Q_OBJECT

private slots:
    void defsNonEmptyAndUnique()
    {
        const QVector<ShortcutDef> defs = shortcutDefs();

        // Non-empty
        QVERIFY(!defs.isEmpty());

        // Contains required entries
        QSet<QString> keys;
        for (const ShortcutDef& d : defs) {
            keys.insert(QString::fromUtf8(d.keys));
        }

        QVERIFY2(keys.contains("F5"),     "Missing F5 (Start/Stop streaming)");
        QVERIFY2(keys.contains("Ctrl+R"), "Missing Ctrl+R (Record)");
        QVERIFY2(keys.contains("Ctrl+M"), "Missing Ctrl+M (Marker)");
        QVERIFY2(keys.contains("F1"),     "Missing F1 (Help)");

        // All keys are unique (no duplicate bindings)
        QCOMPARE(keys.size(), defs.size());
    }

    void helpDialogConstructs()
    {
        // Construct without crashing; do NOT exec() (would block headless)
        ShortcutsHelpDialog dlg;
        // Must have at least some child widgets (labels, button, grid items)
        const QList<QWidget*> children = dlg.findChildren<QWidget*>();
        QVERIFY2(!children.isEmpty(), "ShortcutsHelpDialog has no child widgets");
    }
};

} // namespace studio

QTEST_MAIN(studio::TestShortcuts)
#include "test_shortcuts.moc"
