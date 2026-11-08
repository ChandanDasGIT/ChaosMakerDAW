#pragma once

#include "plugins/chordplayer/chordsettings.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class TrackListWrapper;
class EngineController;

// One Chords track's page in QML: the eight strips' chords, the autoplay, the key (for the
// degree buttons) and the ready-made progressions. Edits go to the track through
// TrackListWrapper::setChords; the engine follows its chordsChanged signal. press() / release()
// play a strip on this track as MIDI notes on its trigger keys (ChordPlayer::ChordKeys /
// BassKeys), so a strip, KEYS and a MIDI keyboard play the same way and recording takes the
// presses. activeStrip follows the player (polled about 30 times a second while watched).
class ChordsWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* trackModel READ trackModel WRITE setTrackModel NOTIFY trackModelChanged)
    Q_PROPERTY(int trackId READ trackId WRITE setTrackId NOTIFY trackIdChanged)
    Q_PROPERTY(QObject* engine READ engine WRITE setEngine NOTIFY engineChanged)
    Q_PROPERTY(bool watchActive READ watchActive WRITE setWatchActive NOTIFY watchActiveChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY settingsChanged)            // a Chords track

    Q_PROPERTY(QStringList chords READ chords NOTIFY settingsChanged)   // eight names, "" = empty strip
    Q_PROPERTY(int autoplay READ autoplay WRITE setAutoplay NOTIFY settingsChanged)
    Q_PROPERTY(int keyTonic READ keyTonic NOTIFY settingsChanged)
    Q_PROPERTY(bool keyMinor READ keyMinor NOTIFY settingsChanged)
    Q_PROPERTY(QString keyName READ keyName NOTIFY settingsChanged)
    Q_PROPERTY(QStringList tonicNames READ tonicNames NOTIFY settingsChanged)   // 12, spelt for the key
    Q_PROPERTY(QStringList degreeLabels READ degreeLabels NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList autoplayModes READ autoplayModes CONSTANT)  // [{mode, title}]
    Q_PROPERTY(QVariantList progressions READ progressions CONSTANT)    // [{index, name, description, minor}]
    Q_PROPERTY(int activeStrip READ activeStrip NOTIFY activeChanged)   // -1: none
    Q_PROPERTY(bool activeBass READ activeBass NOTIFY activeChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit ChordsWrapper(QObject* parent = nullptr);

    QObject* trackModel() const;
    void setTrackModel(QObject* model);
    int trackId() const { return m_trackId; }
    void setTrackId(int id);
    QObject* engine() const;
    void setEngine(QObject* engine);
    bool watchActive() const { return m_watch; }
    void setWatchActive(bool watch);
    bool valid() const;

    QStringList chords() const;
    int autoplay() const { return m_s.autoplay; }
    void setAutoplay(int mode);
    int keyTonic() const { return m_s.key.tonic; }
    bool keyMinor() const { return m_s.key.minor; }
    QString keyName() const;
    QStringList tonicNames() const;
    QStringList degreeLabels() const;
    QVariantList autoplayModes() const;
    QVariantList progressions() const;
    int activeStrip() const { return m_activeStrip; }
    bool activeBass() const { return m_activeBass; }
    QString lastError() const { return m_lastError; }

    // A chord typed for strip 0..7 ("" empties it). False (lastError says why) if it does not read.
    Q_INVOKABLE bool setChord(int strip, const QString& name);
    // The key's chord on degree 1..7 (seventh: the four-note one), and putting it on a strip.
    Q_INVOKABLE QString degreeChord(int degree, bool seventh) const;
    Q_INVOKABLE bool setDegree(int strip, int degree, bool seventh);
    Q_INVOKABLE void setKey(int tonic, bool minor);
    // Fills the eight strips with progression `index` in the key's tonic (in the progression's mode).
    Q_INVOKABLE bool applyProgression(int index);
    Q_INVOKABLE bool resetToDefaults();

    // Plays strip 0..7 (its chord area, or its bass area) on this track, as MIDI.
    Q_INVOKABLE void press(int strip, bool bass, int velocity = 100);
    Q_INVOKABLE void release(int strip, bool bass);
    Q_INVOKABLE int keyOf(int strip, bool bass) const;    // the trigger key, -1 outside 0..7

    // A recorded or drawn Chords clip played out as piano notes on a new Piano track at the same
    // place (the autoplay and the chords as they are now). The new track's id, or -1.
    Q_INVOKABLE int convertClipToPianoTrack(int clipId);

signals:
    void trackModelChanged();
    void trackIdChanged();
    void engineChanged();
    void watchActiveChanged();
    void settingsChanged();
    void activeChanged();
    void lastErrorChanged();

private:
    void reload();
    bool commit(const chords::Settings& settings);
    void pollActive();
    void fail(const QString& message);

    QPointer<TrackListWrapper> m_trackModel;
    QPointer<EngineController> m_engine;
    int m_trackId = -1;
    chords::Settings m_s;
    bool m_watch = false;
    QTimer m_timer;
    int m_activeStrip = -1;
    bool m_activeBass = false;
    std::array<bool, 16> m_down{};                        // strips this page holds (8 chord + 8 bass)
    QString m_lastError;
};
