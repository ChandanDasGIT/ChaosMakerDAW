#pragma once

#include "instruments/electrodrums/electrosettings.h"

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

// One Electronic Drums track's kit controls in QML. Edits go to the track through
// TrackListWrapper::setElectroDrums; the engine follows its electroDrumsChanged signal, and so
// does this wrapper (a second panel on the same track stays in step). It also drives the drum
// machine picture: play() / stop() sound a pad on this track, hit() reports every hit the
// track's kit plays. Drums are named by their ids ("kick", "hihatOpen" ...: electrodrums::drumId).
class ElectroDrumsWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* trackModel READ trackModel WRITE setTrackModel NOTIFY trackModelChanged)
    Q_PROPERTY(int trackId READ trackId WRITE setTrackId NOTIFY trackIdChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY settingsChanged)            // an Electronic Drums track
    // The EngineController: play()/stop() and the hits go through it; without one, nothing.
    Q_PROPERTY(QObject* engine READ engine WRITE setEngine NOTIFY engineChanged)
    // While true, hit() fires for the hits the track's kit plays (polled about 30 times a second).
    Q_PROPERTY(bool watchHits READ watchHits WRITE setWatchHits NOTIFY watchHitsChanged)

    Q_PROPERTY(QString style READ style WRITE setStyle NOTIFY settingsChanged)   // "808", "909", "studio"
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY settingsChanged) // dB
    // Every drum's knobs for bindings: {"kick": {tune, decay, level}, ...}.
    Q_PROPERTY(QVariantMap drumSettings READ drumSettings NOTIFY settingsChanged)

    Q_PROPERTY(QVariantList styles READ styles CONSTANT)                // [{id, title}]
    Q_PROPERTY(QVariantList drums READ drums CONSTANT)                  // [{id, title}]
    // The kit's keys, low to high: [{key, drum (id), name}] (the tight hi-hat ... the ride).
    Q_PROPERTY(QVariantList pads READ pads CONSTANT)
    Q_PROPERTY(double minTune READ minTune CONSTANT)
    Q_PROPERTY(double maxTune READ maxTune CONSTANT)
    Q_PROPERTY(double minDecay READ minDecay CONSTANT)
    Q_PROPERTY(double maxDecay READ maxDecay CONSTANT)
    Q_PROPERTY(double minLevel READ minLevel CONSTANT)
    Q_PROPERTY(double maxLevel READ maxLevel CONSTANT)

public:
    explicit ElectroDrumsWrapper(QObject* parent = nullptr);

    QObject* trackModel() const;
    void setTrackModel(QObject* model);
    int trackId() const { return m_trackId; }
    void setTrackId(int id);
    bool valid() const;

    QString style() const;
    void setStyle(const QString& id);
    double volume() const { return m_s.volumeDb; }
    void setVolume(double v) { edit([v](auto& s) { s.volumeDb = v; }); }

    QVariantMap drumSettings() const;
    // One drum's knobs; an unknown drum id reads 0 and ignores the edit.
    Q_INVOKABLE double tune(const QString& drum) const;
    Q_INVOKABLE double decay(const QString& drum) const;
    Q_INVOKABLE double level(const QString& drum) const;
    Q_INVOKABLE void setTune(const QString& drum, double semitones);
    Q_INVOKABLE void setDecay(const QString& drum, double factor);
    Q_INVOKABLE void setLevel(const QString& drum, double db);
    Q_INVOKABLE QString drumTitle(const QString& drum) const;

    QObject* engine() const;
    void setEngine(QObject* engine);
    bool watchHits() const { return m_watchHits; }
    void setWatchHits(bool watch);

    QVariantList styles() const;
    QVariantList drums() const;
    QVariantList pads() const;
    Q_INVOKABLE QString padName(int key) const;
    Q_INVOKABLE QString padDrum(int key) const;                 // the drum id on `key`, or ""
    Q_INVOKABLE void play(int key, int velocity);
    Q_INVOKABLE void stop(int key);

    double minTune() const { return electrodrums::MinTune; }
    double maxTune() const { return electrodrums::MaxTune; }
    double minDecay() const { return electrodrums::MinDecay; }
    double maxDecay() const { return electrodrums::MaxDecay; }
    double minLevel() const { return electrodrums::MinLevelDb; }
    double maxLevel() const { return electrodrums::MaxLevelDb; }

    Q_INVOKABLE bool resetToDefaults();

signals:
    void trackModelChanged();
    void trackIdChanged();
    void settingsChanged();
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
    bool editDrum(const QString& drum, double electrodrums::DrumSettings::*field, double value);
    const electrodrums::DrumSettings* find(const QString& drum) const;
    bool commit(const electrodrums::Settings& settings);
    void reload();
    void pollHits();

    QPointer<TrackListWrapper> m_trackModel;
    int m_trackId = -1;
    electrodrums::Settings m_s;
    QPointer<EngineController> m_engine;
    bool m_watchHits = false;
    QTimer m_hitTimer;
    std::weak_ptr<Processor> m_watched;                    // the kit the counts below belong to
    std::array<std::uint32_t, 128> m_seen{};
};
