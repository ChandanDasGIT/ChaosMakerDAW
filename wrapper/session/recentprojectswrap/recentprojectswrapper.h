#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include "session/recentprojects.h"

// Exposes the backend RecentProjects list to QML and keeps it in the app settings.
// Every project that is opened or saved (EngineController::projectFileChanged) is added.
class RecentProjectsWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QStringList paths READ paths NOTIFY changed)
    Q_PROPERTY(QStringList labels READ labels NOTIFY changed)
    Q_PROPERTY(bool empty READ empty NOTIFY changed)

public:
    static constexpr const char* SettingsKey = "session/recentProjects";

    explicit RecentProjectsWrapper(QObject* parent = nullptr);

    QStringList paths() const;
    QStringList labels() const;
    bool empty() const noexcept { return m_recent.empty(); }

    Q_INVOKABLE void add(const QUrl& file);
    Q_INVOKABLE void remove(const QString& path);
    Q_INVOKABLE void clear();
    // Drop projects whose file is gone (called before the menu is shown).
    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    void load();
    void save() const;

    RecentProjects m_recent;
};
