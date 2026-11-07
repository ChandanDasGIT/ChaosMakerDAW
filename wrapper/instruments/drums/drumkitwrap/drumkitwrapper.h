#pragma once

#include "instruments/drums/drumkitsettings.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <cstdint>
#include <memory>
#include <QtQml/qqmlregistration.h>

class TrackListWrapper;
class EngineController;
class Processor;

// One Drums track's kit controls in QML. Edits go to the track through
// TrackListWrapper::setDrumKit; the engine follows its drumKitChanged signal, and so does this
// wrapper (a second panel on the same track stays in step). It also tells the page whether the
// kit is still loading or could not be found, and drives the kit picture: play() / stop()
// sound a part on this track, hit() reports every hit the track's kit plays.
class DrumKitWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* trackModel READ trackModel WRITE setTrackModel NOTIFY trackModelChanged)
    Q_PROPERTY(int trackId READ trackId WRITE setTrackId NOTIFY trackIdChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY settingsChanged)            // a Drums track
    // The EngineController: play()/stop() and the hits go through it; without one, nothing.
    Q_PROPERTY(QObject* engine READ engine WRITE setEngine NOTIFY engineChanged)
    // While true, hit() fires for the hits the track's kit plays (polled about 30 times a second).
    Q_PROPERTY(bool watchHits READ watchHits WRITE setWatchHits NOTIFY watchHitsChanged)
    Q_PROPERTY(QVariantMap partKeys READ partKeys CONSTANT)             // "kick" -> 60, ...

    Q_PROPERTY(QString room READ room WRITE setRoom NOTIFY settingsChanged)   // "close", "front", "full"
    Q_PROPERTY(double kick READ kick WRITE setKick NOTIFY settingsChanged)    // dB
    Q_PROPERTY(double snare READ snare WRITE setSnare NOTIFY settingsChanged)
    Q_PROPERTY(double hihat READ hihat WRITE setHihat NOTIFY settingsChanged)
    Q_PROPERTY(double toms READ toms WRITE setToms NOTIFY settingsChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY settingsChanged)
    Q_PROPERTY(double tune READ tune WRITE setTune NOTIFY settingsChanged)    // semitones
    Q_PROPERTY(double micTiming READ micTiming WRITE setMicTiming NOTIFY settingsChanged)     // 0..1
    Q_PROPERTY(double kickBeater READ kickBeater WRITE setKickBeater NOTIFY settingsChanged)  // 0..1
    Q_PROPERTY(double snareBody READ snareBody WRITE setSnareBody NOTIFY settingsChanged)     // 0..1
    Q_PROPERTY(double hihatTrash READ hihatTrash WRITE setHihatTrash NOTIFY settingsChanged)  // 0..1
    Q_PROPERTY(double tomThud READ tomThud WRITE setTomThud NOTIFY settingsChanged)           // 0..1

    // "" when the kit is ready (or not asked for yet); otherwise what the user should know.
    Q_PROPERTY(QString kitStatus READ kitStatus NOTIFY kitStatusChanged)
    Q_PROPERTY(bool kitMissing READ kitMissing NOTIFY kitStatusChanged)

    Q_PROPERTY(QVariantList rooms READ rooms CONSTANT)                  // [{id, title}]
    Q_PROPERTY(double minLevel READ minLevel CONSTANT)
    Q_PROPERTY(double maxLevel READ maxLevel CONSTANT)
    Q_PROPERTY(double maxTune READ maxTune CONSTANT)

public:
    explicit DrumKitWrapper(QObject* parent = nullptr);

    QObject* trackModel() const;
    void setTrackModel(QObject* model);
    int trackId() const { return m_trackId; }
    void setTrackId(int id);
    bool valid() const;

    QString room() const;
    void setRoom(const QString& name);
    double kick() const { return m_s.kickDb; }
    void setKick(double v) { edit([v](auto& s) { s.kickDb = v; }); }
    double snare() const { return m_s.snareDb; }
    void setSnare(double v) { edit([v](auto& s) { s.snareDb = v; }); }
    double hihat() const { return m_s.hihatDb; }
    void setHihat(double v) { edit([v](auto& s) { s.hihatDb = v; }); }
    double toms() const { return m_s.tomsDb; }
    void setToms(double v) { edit([v](auto& s) { s.tomsDb = v; }); }
    double volume() const { return m_s.volumeDb; }
    void setVolume(double v) { edit([v](auto& s) { s.volumeDb = v; }); }
    double tune() const { return m_s.tuneSemitones; }
    void setTune(double v) { edit([v](auto& s) { s.tuneSemitones = v; }); }
    double micTiming() const { return m_s.micTiming; }
    void setMicTiming(double v) { edit([v](auto& s) { s.micTiming = v; }); }
    double kickBeater() const { return m_s.kickBeater; }
    void setKickBeater(double v) { edit([v](auto& s) { s.kickBeater = v; }); }
    double snareBody() const { return m_s.snareBody; }
    void setSnareBody(double v) { edit([v](auto& s) { s.snareBody = v; }); }
    double hihatTrash() const { return m_s.hihatTrash; }
    void setHihatTrash(double v) { edit([v](auto& s) { s.hihatTrash = v; }); }
    double tomThud() const { return m_s.tomThud; }
    void setTomThud(double v) { edit([v](auto& s) { s.tomThud = v; }); }

    QObject* engine() const;
    void setEngine(QObject* engine);
    bool watchHits() const { return m_watchHits; }
    void setWatchHits(bool watch);
    QVariantMap partKeys() const;

    Q_INVOKABLE QString partName(int key) const;
    Q_INVOKABLE void play(int key, int velocity);
    Q_INVOKABLE void stop(int key);

    QString kitStatus() const { return m_status; }
    bool kitMissing() const { return m_missing; }

    QVariantList rooms() const;
    double minLevel() const { return drumkit::MinLevelDb; }
    double maxLevel() const { return drumkit::MaxLevelDb; }
    double maxTune() const { return drumkit::MaxTuneSemitones; }

    Q_INVOKABLE bool resetToDefaults();

signals:
    void trackModelChanged();
    void trackIdChanged();
    void settingsChanged();
    void kitStatusChanged();
    void engineChanged();
    void watchHitsChanged();
    void hit(int key, int velocity);
    void played(int key, int velocity);                      // play() sent this note

private:
    template <typename Change>
    bool edit(Change change)
    {
        if (!valid()) return false;
        auto next = m_s;
        change(next);
        return commit(next);
    }
    bool commit(const drumkit::Settings& settings);
    void reload();
    void pollStatus();
    void pollHits();

    QPointer<TrackListWrapper> m_trackModel;
    int m_trackId = -1;
    drumkit::Settings m_s;
    QString m_status;
    bool m_missing = false;
    QTimer m_statusTimer;
    QPointer<EngineController> m_engine;
    bool m_watchHits = false;
    QTimer m_hitTimer;
    std::weak_ptr<Processor> m_watched;                    // the kit the counts below belong to
    std::array<std::uint32_t, 128> m_seen{};
};
