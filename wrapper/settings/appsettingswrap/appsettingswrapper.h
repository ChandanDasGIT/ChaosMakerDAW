#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include "settings/appsettings.h"

class AppSettingsWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool restoreSessionOnStartup READ restoreSessionOnStartup WRITE setRestoreSessionOnStartup NOTIFY restoreSessionOnStartupChanged)
    Q_PROPERTY(QString defaultProjectDirectory READ defaultProjectDirectory WRITE setDefaultProjectDirectory NOTIFY defaultProjectDirectoryChanged)
    Q_PROPERTY(bool autoSaveEnabled READ autoSaveEnabled WRITE setAutoSaveEnabled NOTIFY autoSaveChanged)
    Q_PROPERTY(int autoSaveIntervalSeconds READ autoSaveIntervalSeconds WRITE setAutoSaveIntervalSeconds NOTIFY autoSaveChanged)

public:
    explicit AppSettingsWrapper(QObject* parent = nullptr);
    explicit AppSettingsWrapper(AppSettings* backend, QObject* parent = nullptr);

    AppSettings& settings() noexcept { return m_settings; }
    const AppSettings& settings() const noexcept { return m_settings; }

    bool restoreSessionOnStartup() const noexcept { return m_settings.restoreSessionOnStartup(); }
    void setRestoreSessionOnStartup(bool enabled);

    QString defaultProjectDirectory() const;
    void setDefaultProjectDirectory(const QString& path);

    bool autoSaveEnabled() const noexcept { return m_settings.autoSaveEnabled(); }
    void setAutoSaveEnabled(bool enabled);

    int autoSaveIntervalSeconds() const noexcept { return m_settings.autoSaveIntervalSeconds(); }
    void setAutoSaveIntervalSeconds(int seconds);

    Q_INVOKABLE void sync();
    Q_INVOKABLE void resetToDefaults();

signals:
    void restoreSessionOnStartupChanged();
    void defaultProjectDirectoryChanged();
    void autoSaveChanged();
    void preferenceSaved(const QString& key);

private:
    void loadFromPlatform();
    void saveToPlatform();

    AppSettings m_ownedSettings;
    AppSettings& m_settings;
};
