#include "testkit.h"
#include "wrapper/settings/shortcutswrap/shortcutswrapper.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

namespace {
// Throwaway settings: the tests never touch the real app's shortcuts.
struct ShortcutStorage {
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-shortcuts-XXXXXX")};
    ShortcutStorage() {
        if (!QCoreApplication::instance()) {
            static int argc = 1; static char name[] = "shortcuttests"; static char* argv[]{name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerShortcutTests");
        QCoreApplication::setApplicationName("Tests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QSettings().clear();
    }
};
}

TEST(qtshortcuts, keys_reach_qml_and_a_change_is_saved_and_read_back)
{
    ShortcutStorage storage;
    ShortcutsWrapper shortcuts;
    int changed = 0;
    QObject::connect(&shortcuts, &ShortcutsWrapper::changed, [&] { ++changed; });
    CHECK(shortcuts.keys()["transport.goToStart"].toString() == "W");
    CHECK(shortcuts.keys()["transport.playPause"].toString() == "Space");
    CHECK(shortcuts.keys()["edit.delete"].toString() == "Del");
    CHECK(shortcuts.texts()["file.saveAs"].toString() == "Ctrl+Shift+S");
    const auto actions = shortcuts.actions();
    CHECK(actions.size() == qsizetype(ShortcutMap::actions().size()));
    CHECK(actions.first().toMap()["group"].toString() == "File" && actions.first().toMap()["isDefault"].toBool());

    CHECK(shortcuts.conflicts("transport.goToStart", "Space") == QStringList{"Play / pause"});
    CHECK(shortcuts.set("transport.goToStart", "Home") && changed == 1);
    CHECK(shortcuts.set("transport.record", "Space"));
    CHECK(shortcuts.keys()["transport.playPause"].toString().isEmpty());
    CHECK(!shortcuts.set("edit.cut", "Escape") && changed == 2);
    CHECK(QSettings().value("shortcuts/transport.goToStart").toString() == "Home");

    // Saved: a new instance (the next start of the app) reads the same keys.
    ShortcutsWrapper again;
    CHECK(again.keys() == shortcuts.keys());
    again.resetAll();
    CHECK(again.keys()["transport.goToStart"].toString() == "W");
    CHECK(QSettings().childGroups().contains("shortcuts") == false);
    shortcuts.reload();
    CHECK(shortcuts.keys()["transport.record"].toString() == "R");
}

TEST(qtshortcuts, a_key_press_becomes_key_text_and_dialog_keys_are_refused)
{
    ShortcutStorage storage;
    ShortcutsWrapper shortcuts;
    CHECK(shortcuts.keyFromEvent(Qt::Key_W, Qt::NoModifier) == "W");
    CHECK(shortcuts.keyFromEvent(Qt::Key_S, int(Qt::ControlModifier | Qt::ShiftModifier)) == "Ctrl+Shift+S");
    CHECK(shortcuts.keyFromEvent(Qt::Key_Delete, Qt::NoModifier) == "Del");
    CHECK(shortcuts.keyFromEvent(Qt::Key_5, int(Qt::KeypadModifier)) == "5");   // keypad: same key
    CHECK(shortcuts.keyFromEvent(Qt::Key_Control, int(Qt::ControlModifier)).isEmpty());
    const auto escape = shortcuts.keyFromEvent(Qt::Key_Escape, Qt::NoModifier);
    CHECK(!escape.isEmpty() && !shortcuts.isAllowed(escape));
    CHECK(shortcuts.isAllowed("Ctrl+Alt+Z") && !shortcuts.isAllowed(""));
    shortcuts.reset("transport.record");
    CHECK(shortcuts.keys()["transport.record"].toString() == "R");
}
