#pragma once

#include "instruments/bass/basssettings.h"
#include "persistence/basspresetstore.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

class TrackListWrapper;

// One Bass Guitar track's controls and presets in QML. Every setting goes by its key
// (bass::controls(): "style", "pickupBlend", "tone" ...): `values` holds them all,
// set() changes one. Edits go to the track through TrackListWrapper::setBass; the engine
// follows its bassChanged signal, and so does this wrapper (a second panel stays in step).
//
// Presets: the factory ones and the user's own (BassPresetStore, in the app data folder's
// "bass-presets"; tests set their own folder). Loading one replaces the track's settings.
class BassWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* trackModel READ trackModel WRITE setTrackModel NOTIFY trackModelChanged)
    Q_PROPERTY(int trackId READ trackId WRITE setTrackId NOTIFY trackIdChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY settingsChanged)            // a Bass Guitar track
    // {key: value}: numbers for Number / Integer, bools for Toggle, choice ids for Choice.
    Q_PROPERTY(QVariantMap values READ values NOTIFY settingsChanged)
    // {key: {kind: "number" | "integer" | "toggle" | "choice", min, max,
    //        choices: [{id, title}] (choice only)}}
    Q_PROPERTY(QVariantMap controls READ controls CONSTANT)
    // Where the user's presets live; empty = the app data folder's "bass-presets".
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY presetsChanged)
    // [{name, factory}]: factory first, then the user's by name.
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    // The preset the track's settings are exactly, or "" once anything is changed. The one last
    // loaded or saved here wins over another with the same settings (a copy saved unchanged).
    Q_PROPERTY(QString currentPreset READ currentPreset NOTIFY currentPresetChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit BassWrapper(QObject* parent = nullptr);
    ~BassWrapper() override;

    QObject* trackModel() const;
    void setTrackModel(QObject* model);
    int trackId() const { return m_trackId; }
    void setTrackId(int id);
    bool valid() const;
    QVariantMap values() const;
    QVariantMap controls() const;
    QString folder() const;
    void setFolder(const QString& folder);
    QVariantList presets() const;
    QString currentPreset() const;
    QString lastError() const { return m_lastError; }

    // One setting: a number (clamped to its range), a bool, or a choice id. False for an
    // unknown key or a value the control does not take.
    Q_INVOKABLE bool set(const QString& key, const QVariant& value);
    Q_INVOKABLE bool resetToDefaults();

    Q_INVOKABLE bool loadPreset(const QString& name);
    // The track's settings as a user preset; replace = overwrite a user preset of that name.
    Q_INVOKABLE bool savePreset(const QString& name, bool replace = false);
    Q_INVOKABLE bool renamePreset(const QString& name, const QString& newName);
    Q_INVOKABLE bool removePreset(const QString& name);
    Q_INVOKABLE bool presetExists(const QString& name) const;   // factory or user
    Q_INVOKABLE bool isFactoryPreset(const QString& name) const;
    Q_INVOKABLE void refreshPresets();                         // re-read the folder

signals:
    void trackModelChanged();
    void trackIdChanged();
    void settingsChanged();
    void presetsChanged();
    void currentPresetChanged();
    void lastErrorChanged();

private:
    bool commit(const bass::Settings& settings);
    void reload();
    BassPresetStore& store() const;
    bool fail(const QString& message);
    void succeed();

    QPointer<TrackListWrapper> m_trackModel;
    int m_trackId = -1;
    bass::Settings m_s;
    QString m_folder;
    QString m_chosen;                                  // the preset last loaded or saved here
    mutable std::unique_ptr<BassPresetStore> m_store;
    QString m_lastError;
};
