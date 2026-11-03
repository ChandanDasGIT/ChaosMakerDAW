#include "session/projectactions.h"

ProjectActions::ProjectActions() = default;
ProjectActions::~ProjectActions() = default;

ProjectActions::ActionType ProjectActions::parseAction(const std::string& action)
{
    if (action == "New") return ActionType::New;
    if (action == "Open") return ActionType::Open;
    if (action == "Close") return ActionType::Close;
    return ActionType::None;
}

std::string ProjectActions::actionToString(ActionType action)
{
    switch (action) {
    case ActionType::New: return "New";
    case ActionType::Open: return "Open";
    case ActionType::Close: return "Close";
    case ActionType::None: default: return "";
    }
}

std::string ProjectActions::pendingActionName() const
{
    return actionToString(m_pendingAction);
}

void ProjectActions::reset() noexcept
{
    m_pendingAction = ActionType::None;
    m_resumeAfterSave = false;
}

ProjectActions::Request ProjectActions::resolveDirect(ActionType action)
{
    switch (action) {
    case ActionType::New:
        return Request::ExecuteNew;
    case ActionType::Open:
        return Request::ShowOpenDialog;
    case ActionType::Close:
        return Request::ExecuteClose;
    case ActionType::None:
    default:
        return Request::None;
    }
}

ProjectActions::Request ProjectActions::requestAction(ActionType action, bool isDirty)
{
    if (action == ActionType::None) return Request::None;
    m_pendingAction = action;

    if (isDirty) {
        return Request::ShowUnsavedConfirmation;
    }

    const auto act = m_pendingAction;
    m_pendingAction = ActionType::None;
    return resolveDirect(act);
}

ProjectActions::Request ProjectActions::requestAction(const std::string& actionName, bool isDirty)
{
    return requestAction(parseAction(actionName), isDirty);
}

ProjectActions::Request ProjectActions::requestSave(bool hasExistingFile, bool resume)
{
    m_resumeAfterSave = resume;
    if (!hasExistingFile) {
        return Request::ShowSaveDialog;
    }
    return Request::None;
}

ProjectActions::Request ProjectActions::requestSaveAs()
{
    m_resumeAfterSave = false;
    return Request::ShowSaveDialog;
}

ProjectActions::Request ProjectActions::onUnsavedSave(bool hasExistingFile)
{
    return requestSave(hasExistingFile, /*resume=*/true);
}

ProjectActions::Request ProjectActions::onUnsavedDiscard()
{
    const auto act = m_pendingAction;
    m_pendingAction = ActionType::None;
    m_resumeAfterSave = false;
    return resolveDirect(act);
}

void ProjectActions::onUnsavedCancel()
{
    reset();
}

ProjectActions::Request ProjectActions::onSaveCompleted()
{
    if (m_resumeAfterSave) {
        const auto act = m_pendingAction;
        reset();
        return resolveDirect(act);
    }
    return Request::None;
}

void ProjectActions::onSaveCancelled()
{
    reset();
}
