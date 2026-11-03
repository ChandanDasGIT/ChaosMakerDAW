#include "projectactionswrapper.h"

ProjectActionsWrapper::ProjectActionsWrapper(QObject* parent)
    : QObject(parent)
    , m_ownedActions()
    , m_actions(m_ownedActions)
{
}

ProjectActionsWrapper::ProjectActionsWrapper(ProjectActions* actions, QObject* parent)
    : QObject(parent)
    , m_ownedActions()
    , m_actions(actions ? *actions : m_ownedActions)
{
}

QString ProjectActionsWrapper::pendingAction() const
{
    return QString::fromStdString(m_actions.pendingActionName());
}

void ProjectActionsWrapper::dispatch(ProjectActions::Request request)
{
    emit pendingActionChanged();
    switch (request) {
    case ProjectActions::Request::ShowUnsavedConfirmation:
        emit showUnsavedConfirmation();
        break;
    case ProjectActions::Request::ShowOpenDialog:
        emit showOpenDialog();
        break;
    case ProjectActions::Request::ShowSaveDialog:
        emit showSaveDialog();
        break;
    case ProjectActions::Request::ExecuteNew:
        emit executeNew();
        break;
    case ProjectActions::Request::ExecuteClose:
        emit executeClose();
        break;
    case ProjectActions::Request::None:
    default:
        break;
    }
}

void ProjectActionsWrapper::requestAction(const QString& action, bool isDirty)
{
    dispatch(m_actions.requestAction(action.toStdString(), isDirty));
}

void ProjectActionsWrapper::requestSave(bool hasExistingFile)
{
    const auto req = m_actions.requestSave(hasExistingFile, false);
    if (req == ProjectActions::Request::None && hasExistingFile) {
        emit directSaveRequested();
    } else {
        dispatch(req);
    }
}

void ProjectActionsWrapper::requestSaveAs()
{
    dispatch(m_actions.requestSaveAs());
}

void ProjectActionsWrapper::onUnsavedSave(bool hasExistingFile)
{
    const auto req = m_actions.onUnsavedSave(hasExistingFile);
    if (req == ProjectActions::Request::None && hasExistingFile) {
        emit directSaveRequested();
    } else {
        dispatch(req);
    }
}

void ProjectActionsWrapper::onUnsavedDiscard()
{
    dispatch(m_actions.onUnsavedDiscard());
}

void ProjectActionsWrapper::onUnsavedCancel()
{
    m_actions.onUnsavedCancel();
    emit pendingActionChanged();
}

void ProjectActionsWrapper::onSaveCompleted()
{
    dispatch(m_actions.onSaveCompleted());
}

void ProjectActionsWrapper::onSaveCancelled()
{
    m_actions.onSaveCancelled();
    emit pendingActionChanged();
}
