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
    // Main window layout, kept between sessions. The window sets these once a splitter drag ends.
    Q_PROPERTY(double trackListWidth READ trackListWidth WRITE setTrackListWidth NOTIFY layoutSizesChanged)
    Q_PROPERTY(double mixerHeight READ mixerHeight WRITE setMixerHeight NOTIFY layoutSizesChanged)
    Q_PROPERTY(double pianoRollDockHeight READ pianoRollDockHeight WRITE setPianoRollDockHeight NOTIFY layoutSizesChanged)

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

    double trackListWidth() const noexcept { return m_settings.trackListWidth(); }
    void setTrackListWidth(double width);
    double mixerHeight() const noexcept { return m_settings.mixerHeight(); }
    void setMixerHeight(double height);
    double pianoRollDockHeight() const noexcept { return m_settings.pianoRollDockHeight(); }
    void setPianoRollDockHeight(double height);
    // Backend limits, for clamping while a splitter is dragged.
    Q_INVOKABLE double clampTrackListWidth(double width) const noexcept { return AppSettings::clampSize(width, AppSettings::TrackListWidthLimits); }
    Q_INVOKABLE double clampMixerHeight(double height) const noexcept { return AppSettings::clampSize(height, AppSettings::MixerHeightLimits); }
    Q_INVOKABLE double clampPianoRollDockHeight(double height) const noexcept { return AppSettings::clampSize(height, AppSettings::PianoRollDockHeightLimits); }

    Q_INVOKABLE void sync();
    Q_INVOKABLE void resetToDefaults();

signals:
    void pianoRollPresentationChanged();
    void defaultProjectDirectoryChanged();
    void autoSaveChanged();
    void layoutSizesChanged();
    void preferenceSaved(const QString& key);

private:
    void loadFromPlatform();
    void saveToPlatform();                                   // every key this wrapper owns
    static void saveValue(const QString& key, const QVariant& value); // one key only
    // Store one layout size (key from AppSettings) if it changed.
    void saveLayoutSize(const char* key, double before, double after);

    AppSettings m_ownedSettings;
    AppSettings& m_settings;
};
