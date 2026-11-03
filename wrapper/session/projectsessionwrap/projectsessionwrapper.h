#pragma once

#include <QObject>
#include <QUrl>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include "session/projectsession.h"

class ProjectSessionWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QUrl projectFile READ projectFile WRITE setProjectFile NOTIFY projectFileChanged)
    Q_PROPERTY(bool projectDirty READ isProjectDirty WRITE setProjectDirty NOTIFY projectDirtyChanged)
    Q_PROPERTY(bool restoreOnStartup READ restoreOnStartup WRITE setRestoreOnStartup NOTIFY restoreOnStartupChanged)

public:
    explicit ProjectSessionWrapper(QObject* parent = nullptr);
    explicit ProjectSessionWrapper(ProjectSession* session, QObject* parent = nullptr);

    ProjectSession& session() noexcept { return m_session; }
    const ProjectSession& session() const noexcept { return m_session; }

    QUrl projectFile() const;
    void setProjectFile(const QUrl& url);

    bool isProjectDirty() const noexcept { return m_session.isDirty(); }
    void setProjectDirty(bool dirty);

    bool restoreOnStartup() const noexcept { return m_session.restoreOnStartup(); }
    void setRestoreOnStartup(bool enable);

signals:
    void projectFileChanged();
    void projectDirtyChanged();
    void restoreOnStartupChanged();

private:
    ProjectSession m_ownedSession;
    ProjectSession& m_session;
};
