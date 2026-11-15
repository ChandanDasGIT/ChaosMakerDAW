#include "projectActionsWrap.h"

ProjectActionsWrap::ProjectActionsWrap(QObject* parent)
    : QObject(parent)
    , m_ownedActions()
    , m_actions(m_ownedActions)
{
}

ProjectActionsWrap::ProjectActionsWrap(ProjectActions* actions, QObject* parent)
    : QObject(parent)
    , m_ownedActions()
    , m_actions(actions ? *actions : m_ownedActions)
{
}

QString ProjectActionsWrap::pendingAction() const
{
    return QString::fromStdString(m_actions.pendingActionName());
}

void ProjectActionsWrap::dispatch(ProjectActions::Request request)
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
    case ProjectActions::Request::ExecuteOpenFile:
        emit executeOpenFile(QUrl::fromLocalFile(QString::fromStdString(m_actions.openFilePath())));
        break;
    case ProjectActions::Request::None:
    default:
        break;
    }
}

void ProjectActionsWrap::requestAction(const QString& action, bool isDirty)
{
    dispatch(m_actions.requestAction(action.toStdString(), isDirty));
}

void ProjectActionsWrap::requestOpenFile(const QString& path, bool isDirty)
{
    dispatch(m_actions.requestOpenFile(path.toStdString(), isDirty));
}

void ProjectActionsWrap::requestSave(bool hasExistingFile)
{
    const auto req = m_actions.requestSave(hasExistingFile, false);
    if (req == ProjectActions::Request::None && hasExistingFile) {
        emit directSaveRequested();
    } else {
        dispatch(req);
    }
}

void ProjectActionsWrap::requestSaveAs()
{
    dispatch(m_actions.requestSaveAs());
}

void ProjectActionsWrap::onUnsavedSave(bool hasExistingFile)
{
    const auto req = m_actions.onUnsavedSave(hasExistingFile);
    if (req == ProjectActions::Request::None && hasExistingFile) {
        emit directSaveRequested();
    } else {
        dispatch(req);
    }
}

void ProjectActionsWrap::onUnsavedDiscard()
{
    dispatch(m_actions.onUnsavedDiscard());
}

void ProjectActionsWrap::onUnsavedCancel()
{
    m_actions.onUnsavedCancel();
    emit pendingActionChanged();
}

void ProjectActionsWrap::onSaveCompleted()
{
    dispatch(m_actions.onSaveCompleted());
}

void ProjectActionsWrap::onSaveCancelled()
{
    m_actions.onSaveCancelled();
    emit pendingActionChanged();
}
