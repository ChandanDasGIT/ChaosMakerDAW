#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class TrackListWrapper;
class PitchMonitor;

// The Pitch Monitor page's model (restMap G18). The pitch of the owner's recorded audio over the
// song (analysed in the background whenever its clips change), the live pitch while recording,
// and a readout of the pitch now, all measured against the effect's key (its "key" and "scale"
// choices, which the page sets through MixEffectWrapper).
class PitchMonitorWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* trackModel READ trackModel WRITE setTrackModel NOTIFY trackModelChanged)
    // A track id, or a clip's effects id (Project::clipEffectsId) for a clip effect.
    Q_PROPERTY(int effectsId READ effectsId WRITE setEffectsId NOTIFY effectsIdChanged)
    // The EngineController, for the live pitch. Without it there is only the recorded line.
    Q_PROPERTY(QObject* engine READ engine WRITE setEngine NOTIFY engineChanged)
    // The page is visible: listen to the effect and poll it about 30 times a second.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int keyTonic READ keyTonic NOTIFY keyChanged)            // 0 = C ... 11 = B
    Q_PROPERTY(int scale READ scale NOTIFY keyChanged)                  // musickey::Scale
    Q_PROPERTY(QString keyName READ keyName NOTIFY keyChanged)          // "A minor", "Chromatic"
    Q_PROPERTY(QStringList tonicNames READ tonicNames CONSTANT)
    Q_PROPERTY(QStringList scaleNames READ scaleNames CONSTANT)
    Q_PROPERTY(bool analysing READ analysing NOTIFY analysingChanged)
    // Any pitch in the recorded line; its lowest and highest notes (whole MIDI numbers).
    Q_PROPERTY(bool hasLine READ hasLine NOTIFY curveChanged)
    Q_PROPERTY(int lowestNote READ lowestNote NOTIFY curveChanged)
    Q_PROPERTY(int highestNote READ highestNote NOTIFY curveChanged)
    // The pitch now (the last 250 ms): the nearest in-key note and how far off it is.
    Q_PROPERTY(bool hasPitch READ hasPitch NOTIFY readingChanged)
    Q_PROPERTY(double pitch READ pitch NOTIFY readingChanged)           // fractional MIDI number
    Q_PROPERTY(int note READ note NOTIFY readingChanged)
    Q_PROPERTY(QString noteName READ noteName NOTIFY readingChanged)
    Q_PROPERTY(double cents READ cents NOTIFY readingChanged)           // + sharp, - flat
    Q_PROPERTY(int liveCount READ liveCount NOTIFY liveChanged)

public:
    // How long a pitch stays in the readout after the voice stops.
    static constexpr int HoldMs = 250;
    static constexpr std::size_t MaxLivePoints = 60000;   // 10 minutes

    explicit PitchMonitorWrapper(QObject* parent = nullptr);
    ~PitchMonitorWrapper() override;

    QObject* trackModel() const;
    void setTrackModel(QObject* model);
    int effectsId() const { return m_effectsId; }
    void setEffectsId(int id);
    QObject* engine() const { return m_engine; }
    void setEngine(QObject* engine);
    bool active() const { return m_active; }
    void setActive(bool on);

    int keyTonic() const;
    int scale() const;
    QString keyName() const;
    QStringList tonicNames() const;
    QStringList scaleNames() const;
    bool analysing() const { return m_analysing; }
    bool hasLine() const { return m_lowest <= m_highest; }
    int lowestNote() const { return hasLine() ? m_lowest : 48; }
    int highestNote() const { return hasLine() ? m_highest : 72; }
    bool hasPitch() const { return m_hasPitch; }
    double pitch() const { return m_pitch; }
    int note() const { return m_note; }
    QString noteName() const;
    double cents() const { return m_cents; }
    int liveCount() const { return int(m_live.size()); }

    // The recorded line between two beats as flat numbers: beat, pitch (fractional MIDI number,
    // 0 = a gap), cents from the nearest in-key note; one triple per 10 ms (thinned to about
    // maxPoints).
    Q_INVOKABLE QVariantList curve(double fromBeat, double toBeat, int maxPoints = 4000) const;
    // The live line (recorded while the transport ran) the same way.
    Q_INVOKABLE QVariantList live(double fromBeat, double toBeat, int maxPoints = 4000) const;
    Q_INVOKABLE bool inKey(int note) const;
    Q_INVOKABLE QString nameOf(int note) const;
    // Analyses the recorded audio again now (it also follows every clip change by itself).
    Q_INVOKABLE void reanalyse();
    // Test hook: one poll of the effect (the timer does it while active).
    Q_INVOKABLE void poll();

signals:
    void trackModelChanged();
    void effectsIdChanged();
    void engineChanged();
    void activeChanged();
    void keyChanged();
    void analysingChanged();
    void curveChanged();
    void readingChanged();
    void liveChanged();

private:
    struct Point { std::int64_t sample; float midi; };
    QVariantList toList(const std::vector<Point>& points, double fromBeat, double toBeat, int maxPoints) const;
    std::vector<std::shared_ptr<PitchMonitor>> processors(bool playback) const;
    void listen(bool on);
    void scheduleAnalysis();
    void startAnalysis();
    void setReading(bool has, double pitch);
    void clearLive();

    QPointer<TrackListWrapper> m_model;
    QPointer<QObject> m_engine;
    int m_effectsId = -1;
    bool m_active = false;
    std::vector<Point> m_points;          // the recorded line, by song sample
    std::vector<Point> m_live;
    int m_lowest = 1, m_highest = 0;      // lowest > highest: no line
    bool m_analysing = false;
    bool m_dirty = true;                  // the clips changed while the page was hidden
    std::uint64_t m_generation = 0;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    bool m_hasPitch = false;
    double m_pitch = 0.0, m_cents = 0.0;
    int m_note = -1;
    QElapsedTimer m_sinceVoiced;
    std::vector<std::weak_ptr<PitchMonitor>> m_listening;
    QTimer m_pollTimer, m_analysisTimer;
    std::vector<QMetaObject::Connection> m_connections;
};
