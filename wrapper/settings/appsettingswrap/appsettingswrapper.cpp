#include "appsettingswrapper.h"

#include <QSettings>
#include <QVariant>

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
    if (qs.contains(QStringLiteral("ui/pianoRollPresentation"))) {
        m_settings.importValue(AppSettings::KeyPianoRollPresentation,
                               qs.value(QStringLiteral("ui/pianoRollPresentation")).toString().toStdString());
    }
    for (const char* key : {AppSettings::KeyTrackListWidth, AppSettings::KeyMixerHeight, AppSettings::KeyPianoRollDockHeight,
                            AppSettings::KeyDefaultRecordMode, AppSettings::KeyChordZoneMapping,
                            AppSettings::KeySnapPlayhead, AppSettings::KeySnapSelection}) {
        const QString name = QString::fromLatin1(key);
        if (qs.contains(name)) m_settings.importValue(key, qs.value(name).toString().toStdString());
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

void AppSettingsWrapper::saveValue(const QString& key, const QVariant& value)
{
    QSettings qs;
    qs.setValue(key, value);
    qs.sync();
}

// Writes only the keys this wrapper owns, never "session/restoreOnStartup" (EngineController's).
void AppSettingsWrapper::saveToPlatform()
{
    QSettings qs;
    qs.setValue(QStringLiteral("ui/pianoRollPresentation"), QString::fromStdString(m_settings.pianoRollPresentation()));
    qs.setValue(QStringLiteral("paths/defaultProjectDirectory"), QString::fromStdString(m_settings.defaultProjectDirectory()));
    qs.setValue(QStringLiteral("autosave/enabled"), m_settings.autoSaveEnabled());
    qs.setValue(QStringLiteral("autosave/intervalSeconds"), m_settings.autoSaveIntervalSeconds());
    for (const char* key : {AppSettings::KeyTrackListWidth, AppSettings::KeyMixerHeight, AppSettings::KeyPianoRollDockHeight,
                            AppSettings::KeyDefaultRecordMode, AppSettings::KeySnapPlayhead, AppSettings::KeySnapSelection})
        qs.setValue(QString::fromLatin1(key), QString::fromStdString(m_settings.exportValue(key)));
    qs.sync();
}

QString AppSettingsWrapper::pianoRollPresentation() const
{
    return QString::fromStdString(m_settings.pianoRollPresentation());
}

void AppSettingsWrapper::setPianoRollPresentation(const QString& presentation)
{
    if (m_settings.pianoRollPresentation() != presentation.toStdString()) {
        m_settings.setPianoRollPresentation(presentation.toStdString());
        saveValue(QStringLiteral("ui/pianoRollPresentation"), QString::fromStdString(m_settings.pianoRollPresentation()));
        emit pianoRollPresentationChanged();
        emit preferenceSaved(QString::fromLatin1(AppSettings::KeyPianoRollPresentation));
    }
}

void AppSettingsWrapper::setDefaultRecordMode(const QString& mode)
{
    if (m_settings.defaultRecordMode() == mode.toStdString()) return;
    m_settings.setDefaultRecordMode(mode.toStdString());          // invalid modes are ignored
    if (m_settings.defaultRecordMode() != mode.toStdString()) return;
    saveValue(QString::fromLatin1(AppSettings::KeyDefaultRecordMode), mode);
    emit defaultRecordModeChanged();
    emit preferenceSaved(QString::fromLatin1(AppSettings::KeyDefaultRecordMode));
}

void AppSettingsWrapper::setChordZoneMapping(const QString& mapping)
{
    if (m_settings.chordZoneMapping() == mapping.toStdString()) return;
    m_settings.setChordZoneMapping(mapping.toStdString());       // invalid choices are ignored
    if (m_settings.chordZoneMapping() != mapping.toStdString()) return;
    saveValue(QString::fromLatin1(AppSettings::KeyChordZoneMapping), mapping);
    emit chordZoneMappingChanged();
    emit preferenceSaved(QString::fromLatin1(AppSettings::KeyChordZoneMapping));
}

QString AppSettingsWrapper::storedChordZoneMapping()
{
    AppSettings settings;
    QSettings qs;
    const QString name = QString::fromLatin1(AppSettings::KeyChordZoneMapping);
    if (qs.contains(name)) settings.importValue(AppSettings::KeyChordZoneMapping, qs.value(name).toString().toStdString());
    return QString::fromStdString(settings.chordZoneMapping());
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
        saveValue(QStringLiteral("paths/defaultProjectDirectory"), QString::fromStdString(m_settings.defaultProjectDirectory()));
        emit defaultProjectDirectoryChanged();
        emit preferenceSaved(QString::fromLatin1(AppSettings::KeyDefaultProjectDir));
    }
}

void AppSettingsWrapper::setSnapPlayhead(bool on)
{
    if (m_settings.snapPlayhead() == on) return;
    m_settings.setSnapPlayhead(on);
    saveValue(QString::fromLatin1(AppSettings::KeySnapPlayhead), QString::fromStdString(m_settings.exportValue(AppSettings::KeySnapPlayhead)));
    emit timelineSnapChanged();
    emit preferenceSaved(QString::fromLatin1(AppSettings::KeySnapPlayhead));
}

void AppSettingsWrapper::setSnapSelection(bool on)
{
    if (m_settings.snapSelection() == on) return;
    m_settings.setSnapSelection(on);
    saveValue(QString::fromLatin1(AppSettings::KeySnapSelection), QString::fromStdString(m_settings.exportValue(AppSettings::KeySnapSelection)));
    emit timelineSnapChanged();
    emit preferenceSaved(QString::fromLatin1(AppSettings::KeySnapSelection));
}

void AppSettingsWrapper::setAutoSaveEnabled(bool enabled)
{
    if (m_settings.autoSaveEnabled() != enabled) {
        m_settings.setAutoSaveEnabled(enabled);
        saveValue(QStringLiteral("autosave/enabled"), m_settings.autoSaveEnabled());
        emit autoSaveChanged();
        emit preferenceSaved(QString::fromLatin1(AppSettings::KeyAutoSaveEnabled));
    }
}

void AppSettingsWrapper::setAutoSaveIntervalSeconds(int seconds)
{
    if (m_settings.autoSaveIntervalSeconds() != seconds) {
        if (m_settings.setAutoSaveIntervalSeconds(seconds)) {
            saveValue(QStringLiteral("autosave/intervalSeconds"), m_settings.autoSaveIntervalSeconds());
            emit autoSaveChanged();
            emit preferenceSaved(QString::fromLatin1(AppSettings::KeyAutoSaveInterval));
        }
    }
}

void AppSettingsWrapper::saveLayoutSize(const char* key, double before, double after)
{
    if (before == after) return;
    saveValue(QString::fromLatin1(key), QString::fromStdString(m_settings.exportValue(key)));
    emit layoutSizesChanged();
    emit preferenceSaved(QString::fromLatin1(key));
}

void AppSettingsWrapper::setTrackListWidth(double width)
{
    const double before = m_settings.trackListWidth();
    m_settings.setTrackListWidth(width);
    saveLayoutSize(AppSettings::KeyTrackListWidth, before, m_settings.trackListWidth());
}

void AppSettingsWrapper::setMixerHeight(double height)
{
    const double before = m_settings.mixerHeight();
    m_settings.setMixerHeight(height);
    saveLayoutSize(AppSettings::KeyMixerHeight, before, m_settings.mixerHeight());
}

void AppSettingsWrapper::setPianoRollDockHeight(double height)
{
    const double before = m_settings.pianoRollDockHeight();
    m_settings.setPianoRollDockHeight(height);
    saveLayoutSize(AppSettings::KeyPianoRollDockHeight, before, m_settings.pianoRollDockHeight());
}

void AppSettingsWrapper::sync()
{
    saveToPlatform();
}

void AppSettingsWrapper::resetToDefaults()
{
    m_settings.resetToDefaults();
    saveToPlatform();
    emit pianoRollPresentationChanged();
    emit defaultProjectDirectoryChanged();
    emit autoSaveChanged();
    emit layoutSizesChanged();
}
