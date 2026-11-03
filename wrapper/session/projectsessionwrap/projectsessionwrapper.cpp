#include "projectsessionwrapper.h"
#include <QFileInfo>

ProjectSessionWrapper::ProjectSessionWrapper(QObject* parent)
    : QObject(parent)
    , m_ownedSession()
    , m_session(m_ownedSession)
{
}

ProjectSessionWrapper::ProjectSessionWrapper(ProjectSession* session, QObject* parent)
    : QObject(parent)
    , m_ownedSession()
    , m_session(session ? *session : m_ownedSession)
{
}

QUrl ProjectSessionWrapper::projectFile() const
{
    const auto& path = m_session.currentFilePath();
    if (path.empty()) return QUrl();
    return QUrl::fromLocalFile(QString::fromStdString(path));
}

void ProjectSessionWrapper::setProjectFile(const QUrl& url)
{
    const std::string path = url.isLocalFile() ? url.toLocalFile().toStdString() : url.toString().toStdString();
    if (m_session.currentFilePath() == path) return;
    m_session.setCurrentFilePath(path);
    emit projectFileChanged();
}

void ProjectSessionWrapper::setProjectDirty(bool dirty)
{
    if (m_session.isDirty() == dirty) return;
    m_session.setDirty(dirty);
    emit projectDirtyChanged();
}

void ProjectSessionWrapper::setRestoreOnStartup(bool enable)
{
    if (m_session.restoreOnStartup() == enable) return;
    m_session.setRestoreOnStartup(enable);
    emit restoreOnStartupChanged();
}
