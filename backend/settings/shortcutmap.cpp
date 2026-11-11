#include "settings/shortcutmap.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace {

std::string lower(std::string_view text)
{
    std::string out(text);
    for (auto& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trimmed(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

// Key names in the form Qt writes them (QKeySequence::PortableText), with common spellings.
std::string keyName(std::string_view key)
{
    if (key.size() == 1) return std::string(1, char(std::toupper(static_cast<unsigned char>(key[0]))));
    const auto k = lower(key);
    static const std::array<std::pair<std::string_view, std::string_view>, 20> names{{
        {"space", "Space"}, {"delete", "Del"}, {"del", "Del"}, {"backspace", "Backspace"},
        {"left", "Left"}, {"right", "Right"}, {"up", "Up"}, {"down", "Down"},
        {"home", "Home"}, {"end", "End"}, {"pgup", "PgUp"}, {"pageup", "PgUp"},
        {"pgdown", "PgDown"}, {"pagedown", "PgDown"}, {"ins", "Ins"}, {"insert", "Ins"},
        {"esc", "Esc"}, {"escape", "Esc"}, {"return", "Return"}, {"enter", "Enter"},
    }};
    for (const auto& [from, to] : names) if (k == from) return std::string(to);
    if (k == "tab" || k == "backtab") return k == "tab" ? "Tab" : "Backtab";
    if (k.size() >= 2 && k[0] == 'f' && std::all_of(k.begin() + 1, k.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        return "F" + k.substr(1);
    return std::string(key);
}

using Context = ShortcutMap::Context;

} // namespace

const std::vector<ShortcutMap::Action>& ShortcutMap::actions()
{
    static const std::vector<Action> list{
        {"file.new", "New project", "File", Context::Main, "Ctrl+N"},
        {"file.open", "Open project…", "File", Context::Main, "Ctrl+O"},
        {"file.save", "Save", "File", Context::Main, "Ctrl+S"},
        {"file.saveAs", "Save as…", "File", Context::Main, "Ctrl+Shift+S"},
        {"file.import", "Import audio…", "File", Context::Main, "Ctrl+I"},
        {"file.export", "Export…", "File", Context::Main, "Ctrl+E"},

        {"edit.undo", "Undo", "Edit", Context::Main, "Ctrl+Z"},
        {"edit.redo", "Redo", "Edit", Context::Main, "Ctrl+Shift+Z"},
        {"edit.history", "Undo history", "Edit", Context::Main, "Ctrl+Alt+Z"},
        {"edit.cut", "Cut", "Edit", Context::Main, "Ctrl+X"},
        {"edit.copy", "Copy", "Edit", Context::Main, "Ctrl+C"},
        {"edit.paste", "Paste", "Edit", Context::Main, "Ctrl+V"},
        {"edit.duplicate", "Duplicate", "Edit", Context::Main, "Ctrl+D"},
        {"edit.delete", "Delete", "Edit", Context::Main, "Del"},
        {"edit.selectAll", "Select all clips", "Edit", Context::Main, "Ctrl+A"},

        {"transport.playPause", "Play / pause", "Transport", Context::Main, "Space"},
        {"transport.record", "Record", "Transport", Context::Main, "R"},
        {"transport.goToStart", "Go to start", "Transport", Context::Main, "W"},
        {"transport.tapTempo", "Tap tempo", "Transport", Context::Main, "Shift+T"},

        {"timeline.split", "Split clip at the playhead", "Timeline", Context::Main, "S"},
        {"timeline.nudgeLeft", "Nudge clips left", "Timeline", Context::Main, "Left"},
        {"timeline.nudgeRight", "Nudge clips right", "Timeline", Context::Main, "Right"},
        {"timeline.envelope", "Show / hide volume envelope", "Timeline", Context::Main, "V"},

        {"view.mixer", "Show / hide mixer", "View", Context::Main, "M"},

        {"pianoroll.delete", "Delete notes", "Piano roll", Context::PianoRoll, "Del"},
        {"pianoroll.deleteAlt", "Delete notes (second key)", "Piano roll", Context::PianoRoll, "Backspace"},
        {"pianoroll.selectAll", "Select all notes", "Piano roll", Context::PianoRoll, "Ctrl+A"},
        {"pianoroll.quantize", "Quantize notes", "Piano roll", Context::PianoRoll, "Q"},
    };
    return list;
}

const ShortcutMap::Action* ShortcutMap::find(std::string_view id)
{
    for (const auto& action : actions()) if (action.id == id) return &action;
    return nullptr;
}

std::optional<std::string> ShortcutMap::normalise(std::string_view text)
{
    text = trimmed(text);
    if (text.empty()) return std::string();
    bool ctrl = false, alt = false, shift = false, meta = false;
    std::string key;
    std::size_t from = 0;
    while (from <= text.size()) {
        const auto plus = text.find('+', from);
        const auto part = trimmed(text.substr(from, plus == std::string_view::npos ? std::string_view::npos : plus - from));
        if (part.empty()) return std::nullopt;                       // "Ctrl+", "++", "Ctrl++"
        const auto p = lower(part);
        if (p == "ctrl" || p == "control") ctrl = true;
        else if (p == "alt") alt = true;
        else if (p == "shift") shift = true;
        else if (p == "meta") meta = true;
        else if (!key.empty()) return std::nullopt;                  // two keys
        else key = keyName(part);
        if (plus == std::string_view::npos) break;
        from = plus + 1;
    }
    if (key.empty()) return std::nullopt;                            // a modifier alone
    if (key == "Esc" || key == "Return" || key == "Enter" || key == "Tab" || key == "Backtab") return std::nullopt;
    std::string out;
    if (ctrl) out += "Ctrl+";
    if (alt) out += "Alt+";
    if (shift) out += "Shift+";
    if (meta) out += "Meta+";
    return out + key;
}

std::string ShortcutMap::key(std::string_view id) const
{
    if (const auto it = m_keys.find(id); it != m_keys.end()) return it->second;
    const auto* action = find(id);
    return action ? std::string(action->defaultKey) : std::string();
}

bool ShortcutMap::isDefault(std::string_view id) const
{
    return m_keys.find(id) == m_keys.end();
}

std::vector<std::string> ShortcutMap::conflicts(std::string_view id, std::string_view text) const
{
    std::vector<std::string> out;
    const auto* action = find(id);
    const auto wanted = normalise(text);
    if (!action || !wanted || wanted->empty()) return out;
    for (const auto& other : actions())
        if (other.id != id && other.context == action->context && key(other.id) == *wanted)
            out.emplace_back(other.id);
    return out;
}

bool ShortcutMap::set(std::string_view id, std::string_view text)
{
    const auto* action = find(id);
    const auto wanted = normalise(text);
    if (!action || !wanted) return false;
    for (const auto& other : conflicts(id, *wanted)) {
        const auto* taken = find(other);
        if (taken->defaultKey.empty()) m_keys.erase(other);
        else m_keys[other] = "";
    }
    if (*wanted == action->defaultKey) m_keys.erase(std::string(id));
    else m_keys[std::string(id)] = *wanted;
    return true;
}

void ShortcutMap::reset(std::string_view id)
{
    const auto* action = find(id);
    if (!action) return;
    // Its default key comes back; whoever has it now in the same context gives it up.
    set(id, action->defaultKey);
}

void ShortcutMap::loadChanges(const std::map<std::string, std::string>& changes)
{
    m_keys.clear();
    for (const auto& [id, key] : changes) set(id, key);
}
