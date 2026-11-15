#include "shortcuteditor.h"

#include "common/shortcutmap.h"

shortcuteditor::Offer shortcuteditor::judge(std::string_view key, bool allowed, std::size_t conflictCount)
{
    if (key.empty()) return Offer::KeepWaiting;
    if (!allowed) return Offer::Reserved;
    return conflictCount == 0 ? Offer::Assign : Offer::Conflict;
}

std::string shortcuteditor::actionName(std::string_view id)
{
    const auto* action = ShortcutMap::find(id);
    return std::string(action ? action->name : id);
}

std::string shortcuteditor::captureMessage(std::string_view actionName)
{
    return "Press the new key for “" + std::string(actionName) + "” · Esc cancels";
}

std::string shortcuteditor::reservedMessage(std::string_view keyText)
{
    return std::string(keyText) + " is kept for dialogs (cancel, confirm, move focus). Try another key.";
}

std::string shortcuteditor::conflictMessage(std::string_view keyText, const std::vector<std::string>& takers)
{
    std::string names;
    for (std::size_t i = 0; i < takers.size(); ++i) names += (i ? "”, “" : "") + takers[i];
    return std::string(keyText) + " is used by “" + names + "”.";
}
