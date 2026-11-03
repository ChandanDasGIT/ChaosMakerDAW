#include "appsettingswrapper.h"

#include <QSettings>

AppSettingsWrapper::AppSettingsWrapper(QObject* parent)
    : QObject(parent)
    , m_settings(m_ownedSettings)
{
    loadFromPlatform();
}

AppSettingsWrapper::AppSettingsWrapper(AppSettings* backend, QObject* parent)
    : QObject(parent)
    , m_settings(backend ? *backend : m_ownedSettings)
{
    loadFromPlatform();
}

void AppSettingsWrapper::loadFromPlatform()
{
    QSettings qs;
    if (qs.contains(QStringLiteral("session/restoreOnStartup"))) {
        m_settings.importValue(AppSettings::KeyRestoreSessionOnStartup,
                               qs.value(QStringLiteral("session/restoreOnStartup")).toBool() ? "true" : "false");
    }
    if (qs.contains(QStringLiteral("paths/defaultProjectDirectory"))) {
        m_settings.importValue(AppSettings::KeyDefaultProjectDir,
                               qs.value(QStringLiteral("paths/defaultProjectDirectory")).toString().toStdString());
    }
    if (qs.contains(QStringLiteral("autosave/enabled"))) {
        m_settings.importValue(AppSettings::KeyAutoSaveEnabled,
                               qs.value(QStringLiteral("autosave/enabled")).toBool() ? "true" : "false");
    }
    if (qs.contains(QStringLiteral("autosave/intervalSeconds"))) {
        m_settings.importValue(AppSettings::KeyAutoSaveInterval,
                               qs.value(QStringLiteral("autosave/intervalSeconds")).toString().toStdString());
    }
}

void AppSettingsWrapper::saveToPlatform()
{
    QSettings qs;
    qs.setValue(QStringLiteral("session/restoreOnStartup"), m_settings.restoreSessionOnStartup());
    qs.setValue(QStringLiteral("paths/defaultProjectDirectory"), QString::fromStdString(m_settings.defaultProjectDirectory()));
    qs.setValue(QStringLiteral("autosave/enabled"), m_settings.autoSaveEnabled());
    qs.setValue(QStringLiteral("autosave/intervalSeconds"), m_settings.autoSaveIntervalSeconds());
    qs.sync();
}

void AppSettingsWrapper::setRestoreSessionOnStartup(bool enabled)
{
    if (m_settings.restoreSessionOnStartup() != enabled) {
        m_settings.setRestoreSessionOnStartup(enabled);
        saveToPlatform();
        emit restoreSessionOnStartupChanged();
        emit preferenceSaved(QString::fromLatin1(AppSettings::KeyRestoreSessionOnStartup));
    }
}

QString AppSettingsWrapper::defaultProjectDirectory() const
{
    return QString::fromStdString(m_settings.defaultProjectDirectory());
}

void AppSettingsWrapper::setDefaultProjectDirectory(const QString& path)
{
    std::string pathStd = path.toStdString();
    if (m_settings.defaultProjectDirectory() != pathStd) {
        m_settings.setDefaultProjectDirectory(std::move(pathStd));
        saveToPlatform();
        emit defaultProjectDirectoryChanged();
        emit preferenceSaved(QString::fromLatin1(AppSettings::KeyDefaultProjectDir));
    }
}

void AppSettingsWrapper::setAutoSaveEnabled(bool enabled)
{
    if (m_settings.autoSaveEnabled() != enabled) {
        m_settings.setAutoSaveEnabled(enabled);
        saveToPlatform();
        emit autoSaveChanged();
        emit preferenceSaved(QString::fromLatin1(AppSettings::KeyAutoSaveEnabled));
    }
}

void AppSettingsWrapper::setAutoSaveIntervalSeconds(int seconds)
{
    if (m_settings.autoSaveIntervalSeconds() != seconds) {
        if (m_settings.setAutoSaveIntervalSeconds(seconds)) {
            saveToPlatform();
            emit autoSaveChanged();
            emit preferenceSaved(QString::fromLatin1(AppSettings::KeyAutoSaveInterval));
        }
    }
}

void AppSettingsWrapper::sync()
{
    saveToPlatform();
}

void AppSettingsWrapper::resetToDefaults()
{
    m_settings.resetToDefaults();
    saveToPlatform();
    emit restoreSessionOnStartupChanged();
    emit defaultProjectDirectoryChanged();
    emit autoSaveChanged();
}
