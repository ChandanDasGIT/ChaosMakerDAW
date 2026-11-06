#include "recentprojectswrapper.h"

#include <QSettings>
#include "enginecontroller.h"

RecentProjectsWrapper::RecentProjectsWrapper(QObject* parent)
    : QObject(parent)
{
    load();
    auto* engine = EngineController::instance();
    connect(engine, &EngineController::projectFileChanged, this, [this, engine] { add(engine->projectFile()); });
}

QStringList RecentProjectsWrapper::paths() const
{
    QStringList result;
    for (const auto& path : m_recent.paths()) result << QString::fromStdString(path);
    return result;
}

QStringList RecentProjectsWrapper::labels() const
{
    QStringList result;
    for (const auto& label : m_recent.labels()) result << QString::fromStdString(label);
    return result;
}

void RecentProjectsWrapper::add(const QUrl& file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (path.isEmpty()) return;
    const auto before = m_recent.paths();
    m_recent.add(path.toStdString());
    if (m_recent.paths() == before) return;
    save();
    emit changed();
}

void RecentProjectsWrapper::remove(const QString& path)
{
    if (!m_recent.remove(path.toStdString())) return;
    save();
    emit changed();
}

void RecentProjectsWrapper::clear()
{
    if (m_recent.empty()) return;
    m_recent.clear();
    save();
    emit changed();
}

void RecentProjectsWrapper::refresh()
{
    if (m_recent.removeMissing() == 0) return;
    save();
    emit changed();
}

void RecentProjectsWrapper::load()
{
    std::vector<std::string> stored;
    for (const auto& path : QSettings().value(QString::fromLatin1(SettingsKey)).toStringList())
        stored.push_back(path.toStdString());
    m_recent.setPaths(stored);
}

void RecentProjectsWrapper::save() const
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(SettingsKey), paths());
    settings.sync();
}
