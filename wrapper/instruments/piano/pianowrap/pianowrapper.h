#pragma once

#include "instruments/piano/pianosettings.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>

#include <array>
#include <cstdint>
#include <memory>
#include <QtQml/qqmlregistration.h>

class TrackListWrapper;
class EngineController;
class Processor;

// One Piano track's controls in QML. Edits go to the track through TrackListWrapper::setPiano;
// the engine follows its pianoChanged signal, and so does this wrapper (a second panel on the
// same track stays in step). It also says whether the piano is still loading or could not be
// found, and drives the drawn piano: play() / stop() / setPedalDown() sound the track, and while
// watchKeys is on, keysDown and pedal follow what the track's piano plays (from any source).
class PianoWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* trackModel READ trackModel WRITE setTrackModel NOTIFY trackModelChanged)
    Q_PROPERTY(int trackId READ trackId WRITE setTrackId NOTIFY trackIdChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY settingsChanged)            // a Piano track
    Q_PROPERTY(QObject* engine READ engine WRITE setEngine NOTIFY engineChanged)
    // While true, keysDown, pedal and struck() follow the track's piano (about 30 times a second).
    Q_PROPERTY(bool watchKeys READ watchKeys WRITE setWatchKeys NOTIFY watchKeysChanged)
    Q_PROPERTY(QVariantMap keysDown READ keysDown NOTIFY keysChanged)  // "60" -> velocity, held keys only
    Q_PROPERTY(int pedal READ pedal NOTIFY keysChanged)                 // CC 64, 0..127

    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY settingsChanged)        // dB
    Q_PROPERTY(double release READ release WRITE setRelease NOTIFY settingsChanged)     // 0..1
    Q_PROPERTY(double resonance READ resonance WRITE setResonance NOTIFY settingsChanged) // 0..1
    Q_PROPERTY(double velocity READ velocity WRITE setVelocity NOTIFY settingsChanged)  // 0..1
    Q_PROPERTY(double brightness READ brightness WRITE setBrightness NOTIFY settingsChanged) // 0..1
    Q_PROPERTY(int transpose READ transpose WRITE setTranspose NOTIFY settingsChanged)  // semitones

    // "" when the piano is ready (or not asked for yet); otherwise what the user should know.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool missing READ missing NOTIFY statusChanged)

    Q_PROPERTY(double minVolume READ minVolume CONSTANT)
    Q_PROPERTY(double maxVolume READ maxVolume CONSTANT)
    Q_PROPERTY(int maxTranspose READ maxTranspose CONSTANT)
    Q_PROPERTY(int lowestKey READ lowestKey CONSTANT)                   // A0, the piano's range
    Q_PROPERTY(int highestKey READ highestKey CONSTANT)                 // C8

public:
    explicit PianoWrapper(QObject* parent = nullptr);

    QObject* trackModel() const;
    void setTrackModel(QObject* model);
    int trackId() const { return m_trackId; }
    void setTrackId(int id);
    bool valid() const;

    double volume() const { return m_s.volumeDb; }
    void setVolume(double v) { edit([v](auto& s) { s.volumeDb = v; }); }
    double release() const { return m_s.release; }
    void setRelease(double v) { edit([v](auto& s) { s.release = v; }); }
    double resonance() const { return m_s.resonance; }
    void setResonance(double v) { edit([v](auto& s) { s.resonance = v; }); }
    double velocity() const { return m_s.velocity; }
    void setVelocity(double v) { edit([v](auto& s) { s.velocity = v; }); }
    double brightness() const { return m_s.brightness; }
    void setBrightness(double v) { edit([v](auto& s) { s.brightness = v; }); }
    int transpose() const { return m_s.transpose; }
    void setTranspose(int v) { edit([v](auto& s) { s.transpose = v; }); }

    QObject* engine() const;
    void setEngine(QObject* engine);
    bool watchKeys() const { return m_watchKeys; }
    void setWatchKeys(bool watch);
    QVariantMap keysDown() const { return m_keysDown; }
    int pedal() const { return m_pedal; }

    Q_INVOKABLE void play(int key, int velocity);
    Q_INVOKABLE void stop(int key);
    Q_INVOKABLE void setPedalDown(bool down);           // the drawn pedal: CC 64 at 127 or 0
    Q_INVOKABLE QString noteName(int key) const;        // "C4"

    QString status() const { return m_status; }
    bool missing() const { return m_missing; }

    double minVolume() const { return piano::MinVolumeDb; }
    double maxVolume() const { return piano::MaxVolumeDb; }
    int maxTranspose() const { return piano::MaxTranspose; }
    int lowestKey() const { return 21; }
    int highestKey() const { return 108; }

    Q_INVOKABLE bool resetToDefaults();

signals:
    void trackModelChanged();
    void trackIdChanged();
    void settingsChanged();
    void statusChanged();
    void engineChanged();
    void watchKeysChanged();
    void keysChanged();
    void struck(int key, int velocity);                  // a key went down (even between two looks)
    void played(int key, int velocity);                  // play() sent this note
    void pedalSent(bool down);                           // setPedalDown() sent the pedal

private:
    template <typename Change>
    bool edit(Change change)
    {
        if (!valid()) return false;
        auto next = m_s;
        change(next);
        return commit(next);
    }
    bool commit(const piano::Settings& settings);
    void reload();
    void pollStatus();
    void pollKeys();

    QPointer<TrackListWrapper> m_trackModel;
    int m_trackId = -1;
    piano::Settings m_s;
    QString m_status;
    bool m_missing = false;
    QTimer m_statusTimer;
    QPointer<EngineController> m_engine;
    bool m_watchKeys = false;
    QTimer m_keyTimer;
    std::weak_ptr<Processor> m_watched;                 // the piano the counts below belong to
    std::array<std::uint32_t, 128> m_seen{};
    QVariantMap m_keysDown;
    int m_pedal = 0;
};
