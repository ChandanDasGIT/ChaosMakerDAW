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
// play a strip on this track as MIDI notes on its trigger keys (ChordPlayer::triggerNotes:
// the zone in its own octave, or a zone key first, as Settings > Preferences says), so a strip,
// KEYS and a MIDI keyboard play the same way and recording takes the presses. pressWhole()
// is the strip's name: its middle zone (with its bass when bass with chords is on; the page
// then lights the BASS area too, as for any chord area).
// activeStrip / activeZone follow the player (polled about 30 times a second while watched).
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
    Q_PROPERTY(int dynamics READ dynamics WRITE setDynamics NOTIFY settingsChanged)   // 0 same volume, 1 top note, 2 meter accents
    Q_PROPERTY(bool bassWithChords READ bassWithChords WRITE setBassWithChords NOTIFY settingsChanged)
    Q_PROPERTY(int keyTonic READ keyTonic NOTIFY settingsChanged)
    Q_PROPERTY(bool keyMinor READ keyMinor NOTIFY settingsChanged)
    Q_PROPERTY(QString keyName READ keyName NOTIFY settingsChanged)
    Q_PROPERTY(QStringList tonicNames READ tonicNames CONSTANT)   // 12: "C", "C#/Db", "D", "D#/Eb" ...
    Q_PROPERTY(QStringList degreeLabels READ degreeLabels NOTIFY settingsChanged)
    Q_PROPERTY(int variation READ variation NOTIFY settingsChanged)    // of the autoplay's style
    // The autoplay list: Off, then every style's variations ("Flowing · Rising" ...), as
    // [{index, mode, variation, style, name, title, description}]; autoplayChoice is the entry playing.
    Q_PROPERTY(QVariantList autoplayModes READ autoplayModes CONSTANT)
    // The same, for the AUTOPLAY menu: the styles, each with its variations, as
    // [{mode, title, variations: [{index, name, description}]}] (index into autoplayModes).
    Q_PROPERTY(QVariantList autoplayStyles READ autoplayStyles CONSTANT)
    Q_PROPERTY(int autoplayChoice READ autoplayChoice NOTIFY settingsChanged)
    // Per strip, its zones' ranges for its chord: [low, middle, high], "G3–G5" ...; [] for an empty strip.
    Q_PROPERTY(QVariantList zoneRanges READ zoneRanges NOTIFY settingsChanged)
    // [{index, name, description}], named for the key's mode ("Pop I–V–vi–IV" / "Pop i–v–VI–iv").
    Q_PROPERTY(QVariantList progressions READ progressions NOTIFY settingsChanged)
    // The progression the strips hold in the key, or -1 (chords of your own).
    Q_PROPERTY(int currentProgression READ currentProgression NOTIFY settingsChanged)
    Q_PROPERTY(int activeStrip READ activeStrip NOTIFY activeChanged)   // -1: none
    Q_PROPERTY(bool activeBass READ activeBass NOTIFY activeChanged)
    Q_PROPERTY(int activeZone READ activeZone NOTIFY activeChanged)    // 0 low, 1 middle, 2 high
    Q_PROPERTY(bool activeWhole READ activeWhole NOTIFY activeChanged) // played from its name
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
    void setAutoplay(int mode);                          // a new style starts on its first variation
    int variation() const { return m_s.variation; }
    int autoplayChoice() const;
    // Picks entry `index` of autoplayModes (its style and variation).
    Q_INVOKABLE void setAutoplayChoice(int index);
    int dynamics() const { return m_s.dynamics; }
    void setDynamics(int dynamics);
    bool bassWithChords() const { return m_s.bassWithChords; }
    void setBassWithChords(bool on);
    int keyTonic() const { return m_s.key.tonic; }
    bool keyMinor() const { return m_s.key.minor; }
    QString keyName() const;
    QStringList tonicNames() const;
    QStringList degreeLabels() const;
    QVariantList autoplayModes() const;
    QVariantList autoplayStyles() const;
    QVariantList zoneRanges() const;
    QVariantList progressions() const;
    int currentProgression() const;
    int activeStrip() const { return m_activeStrip; }
    bool activeBass() const { return m_activeBass; }
    int activeZone() const { return m_activeZone; }
    bool activeWhole() const { return m_activeStrip >= 0 && m_activeStrip == m_whole && !m_activeBass && m_activeZone == 1; }
    QString lastError() const { return m_lastError; }

    // A chord typed for strip 0..7 ("" empties it). False (lastError says why) if it does not read.
    Q_INVOKABLE bool setChord(int strip, const QString& name);
    // The key's chord on degree 1..7 (seventh: the four-note one), and putting it on a strip.
    Q_INVOKABLE QString degreeChord(int degree, bool seventh) const;
    Q_INVOKABLE bool setDegree(int strip, int degree, bool seventh);
    // A new key or mode refills the strips: the same progression in the new key (chords of your
    // own become the key chords).
    Q_INVOKABLE void setKey(int tonic, bool minor);
    // Fills the eight strips with progression `index` in the key (its form for the key's mode).
    Q_INVOKABLE bool applyProgression(int index);
    Q_INVOKABLE bool resetToDefaults();

    // Plays strip 0..7 (its chord area in zone 0 low / 1 middle / 2 high, or its bass area) on
    // this track, as MIDI.
    Q_INVOKABLE void press(int strip, bool bass, int velocity = 100, int zone = 1);
    Q_INVOKABLE void release(int strip, bool bass);
    Q_INVOKABLE void pressWhole(int strip, int velocity = 100);
    Q_INVOKABLE void releaseWhole(int strip);
    Q_INVOKABLE int keyOf(int strip, bool bass) const;    // the middle zone's trigger key, -1 outside 0..7

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
    int m_activeZone = 1;
    int m_whole = -1;                                     // the strip last played from its name
    std::array<int, 16> m_down{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};   // key held per strip (8 chord + 8 bass)
    QString m_lastError;
};
