#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Keyboard shortcuts the user can change (restMap G16). Pure C++: keys are text in the portable
// form "Ctrl+Shift+S" (modifiers Ctrl, Alt, Shift, Meta, then one key name such as "S", "Space",
// "Delete", "Left", "F5"); the Qt side turns key presses into this text.
//
// Each action has a fixed id, a name, a group (for the editor) and a default key. Actions live
// in a context: the piano roll's keys only work while it has the keyboard, so they may share a
// key with the main window's (Delete deletes notes there, clips here); within one context a key
// belongs to one action. Escape, Return / Enter and Tab are never given out (they cancel,
// confirm and move focus in every dialog), and the typing keyboard's note keys stay its own
// while KEYS is on (it claims them before any shortcut).
class ShortcutMap {
public:
    enum class Context { Main, PianoRoll };
    struct Action {
        std::string_view id;
        std::string_view name;
        std::string_view group;
        Context context;
        std::string_view defaultKey;
    };
    // Every action, in the editor's order (grouped).
    static const std::vector<Action>& actions();
    static const Action* find(std::string_view id);

    // "ctrl + shift+s" -> "Ctrl+Shift+S"; modifiers in the order Ctrl, Alt, Shift, Meta; a single
    // letter upper case. nullopt for empty text, a modifier alone, two keys, or a key that is
    // never given out (see above). "" stays "" (no key).
    static std::optional<std::string> normalise(std::string_view key);

    // The action's key now: the user's choice, else the default. "" = no key. Unknown id: "".
    std::string key(std::string_view id) const;
    bool isDefault(std::string_view id) const;
    // Other actions in the same context that use `key` now (what assigning it would take over).
    std::vector<std::string> conflicts(std::string_view id, std::string_view key) const;
    // Gives the action `key` ("" clears it). Any other action of the same context that had the
    // key loses it (its key becomes ""). False, and no change, for an unknown id or a key
    // normalise() refuses.
    bool set(std::string_view id, std::string_view key);
    void reset(std::string_view id);
    void resetAll() { m_keys.clear(); }

    // The user's changes only (id -> key, "" = cleared), for saving; loading ignores unknown
    // ids and refused keys, and drops entries equal to the default.
    const std::map<std::string, std::string, std::less<>>& changes() const noexcept { return m_keys; }
    void loadChanges(const std::map<std::string, std::string>& changes);

private:
    std::map<std::string, std::string, std::less<>> m_keys;
};
