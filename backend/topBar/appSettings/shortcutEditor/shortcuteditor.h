#pragma once

#include <string>
#include <string_view>
#include <vector>

// The keyboard shortcuts editor's rules: what a pressed key leads to, and what it says.
namespace shortcuteditor {

// What a key pressed while waiting for one leads to.
enum class Offer {
    KeepWaiting, // a modifier alone ("")
    Reserved,    // kept for dialogs (Esc, Enter, Tab...)
    Assign,      // free: take it
    Conflict     // other actions use it: ask first
};
Offer judge(std::string_view key, bool allowed, std::size_t conflictCount);

// The action's name as the editor lists it; the id itself for an unknown id.
std::string actionName(std::string_view id);

std::string captureMessage(std::string_view actionName);
std::string reservedMessage(std::string_view keyText);
std::string conflictMessage(std::string_view keyText, const std::vector<std::string>& takers);

} // namespace shortcuteditor
