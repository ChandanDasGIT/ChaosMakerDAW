#include "testkit.h"
#include "settings/shortcutmap.h"

#include <set>

TEST(shortcutmap, every_action_has_a_unique_id_and_its_default_is_free_in_its_context)
{
    std::set<std::string_view> ids;
    std::set<std::pair<int, std::string>> keys;
    for (const auto& action : ShortcutMap::actions()) {
        CHECK(ids.insert(action.id).second);
        CHECK(!action.name.empty() && !action.group.empty());
        const auto key = ShortcutMap::normalise(action.defaultKey);
        CHECK(key && *key == action.defaultKey);                    // defaults are already normal
        CHECK(keys.insert({int(action.context), *key}).second);     // no clash out of the box
    }
    // The keys the user asked for (restMap G16).
    ShortcutMap map;
    CHECK(map.key("transport.goToStart") == "W");
    CHECK(map.key("transport.playPause") == "Space");
    CHECK(map.key("nope").empty() && ShortcutMap::find("nope") == nullptr);
}

TEST(shortcutmap, key_text_is_normalised_and_dialog_keys_are_never_given_out)
{
    CHECK(ShortcutMap::normalise(" shift + ctrl+s ") == std::optional<std::string>("Ctrl+Shift+S"));
    CHECK(ShortcutMap::normalise("meta+alt+F5") == std::optional<std::string>("Alt+Meta+F5"));
    CHECK(ShortcutMap::normalise("delete") == std::optional<std::string>("Del"));
    CHECK(ShortcutMap::normalise("Control+PageUp") == std::optional<std::string>("Ctrl+PgUp"));
    CHECK(ShortcutMap::normalise("w") == std::optional<std::string>("W"));
    CHECK(ShortcutMap::normalise("") == std::optional<std::string>(""));
    for (const char* bad : {"Ctrl", "Ctrl+", "A+B", "Ctrl++", "Escape", "Esc", "Shift+Return", "Enter", "Tab", "Shift+Backtab"})
        CHECK(!ShortcutMap::normalise(bad));
}

TEST(shortcutmap, assigning_a_used_key_takes_it_over_within_the_same_context_only)
{
    ShortcutMap map;
    // Delete is both a main-window key (clips) and a piano-roll key (notes): no conflict.
    CHECK(map.conflicts("edit.delete", "Del").empty());
    CHECK(map.conflicts("transport.goToStart", "Space") == std::vector<std::string>{"transport.playPause"});

    CHECK(map.set("transport.goToStart", "Home"));
    CHECK(map.key("transport.goToStart") == "Home" && !map.isDefault("transport.goToStart"));
    // Taking Space from Play / pause leaves that action with no key.
    CHECK(map.set("transport.record", "space"));
    CHECK(map.key("transport.record") == "Space" && map.key("transport.playPause").empty());
    CHECK(map.key("pianoroll.delete") == "Del");
    CHECK(map.set("pianoroll.quantize", "Del"));                // the piano roll's own Delete goes
    CHECK(map.key("pianoroll.delete").empty() && map.key("edit.delete") == "Del");

    // Refused: unknown action, dialog key. Nothing changes.
    const auto before = map.changes();
    CHECK(!map.set("nope", "K") && !map.set("edit.cut", "Escape") && !map.set("edit.cut", "Ctrl+"));
    CHECK(map.changes() == before);

    // Reset brings the default back and takes it from whoever has it now.
    map.reset("transport.playPause");
    CHECK(map.key("transport.playPause") == "Space" && map.isDefault("transport.playPause"));
    CHECK(map.key("transport.record").empty());
    // Setting the default by hand is the same as a reset (nothing to save).
    CHECK(map.set("transport.record", "R") && map.isDefault("transport.record"));
    // Clearing a key.
    CHECK(map.set("view.mixer", "") && map.key("view.mixer").empty() && !map.isDefault("view.mixer"));
    map.resetAll();
    CHECK(map.changes().empty() && map.key("view.mixer") == "M" && map.key("pianoroll.delete") == "Del");
}

TEST(shortcutmap, saved_changes_load_back_and_bad_entries_are_ignored)
{
    ShortcutMap map;
    CHECK(map.set("transport.goToStart", "Home"));
    CHECK(map.set("transport.record", "Space"));
    std::map<std::string, std::string> saved(map.changes().begin(), map.changes().end());
    CHECK(saved.size() == 3);                                    // goToStart, record, playPause ""

    ShortcutMap loaded;
    loaded.loadChanges(saved);
    for (const auto& action : ShortcutMap::actions()) CHECK(loaded.key(action.id) == map.key(action.id));

    saved["nope"] = "K";
    saved["edit.cut"] = "Escape";                                // refused: keeps the default
    saved["edit.copy"] = "Ctrl+C";                               // the default: nothing kept
    loaded.loadChanges(saved);
    CHECK(loaded.key("edit.cut") == "Ctrl+X" && loaded.isDefault("edit.copy"));
    CHECK(loaded.changes().size() == 3 && loaded.key("transport.record") == "Space");
}
