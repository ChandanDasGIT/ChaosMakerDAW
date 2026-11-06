#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include "settings/appsettings.h"

class AppSettingsWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // "Restore session on startup" is owned by EngineController (it applies the setting);
    // this wrapper does not keep a second copy of it.
    Q_PROPERTY(QString pianoRollPresentation READ pianoRollPresentation WRITE setPianoRollPresentation NOTIFY pianoRollPresentationChanged)
    Q_PROPERTY(QString defaultProjectDirectory READ defaultProjectDirectory WRITE setDefaultProjectDirectory NOTIFY defaultProjectDirectoryChanged)
    Q_PROPERTY(bool autoSaveEnabled READ autoSaveEnabled WRITE setAutoSaveEnabled NOTIFY autoSaveChanged)
    Q_PROPERTY(int autoSaveIntervalSeconds READ autoSaveIntervalSeconds WRITE setAutoSaveIntervalSeconds NOTIFY autoSaveChanged)

public:
    explicit AppSettingsWrapper(QObject* parent = nullptr);
    explicit AppSettingsWrapper(AppSettings* backend, QObject* parent = nullptr);

    AppSettings& settings() noexcept { return m_settings; }
    const AppSettings& settings() const noexcept { return m_settings; }

    QString pianoRollPresentation() const;
    void setPianoRollPresentation(const QString& presentation);

    QString defaultProjectDirectory() const;
    void setDefaultProjectDirectory(const QString& path);

    bool autoSaveEnabled() const noexcept { return m_settings.autoSaveEnabled(); }
    void setAutoSaveEnabled(bool enabled);

    int autoSaveIntervalSeconds() const noexcept { return m_settings.autoSaveIntervalSeconds(); }
    void setAutoSaveIntervalSeconds(int seconds);

    Q_INVOKABLE void sync();
    Q_INVOKABLE void resetToDefaults();

signals:
    void pianoRollPresentationChanged();
    void defaultProjectDirectoryChanged();
    void autoSaveChanged();
    void preferenceSaved(const QString& key);

private:
    void loadFromPlatform();
    void saveToPlatform();                                   // every key this wrapper owns
    static void saveValue(const QString& key, const QVariant& value); // one key only

    AppSettings m_ownedSettings;
    AppSettings& m_settings;
};
