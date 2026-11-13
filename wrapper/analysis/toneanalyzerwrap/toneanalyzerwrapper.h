#pragma once

#include "analysis/tonematch.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

class TrackListWrapper;

// The guitar tone analyzer's bridge (restMap G19), for Tools > Guitar tone analyzer…: analyses
// the selected audio clip (backend/analysis tonematch, on a worker thread with progress and
// cancel), keeps the pedalboard it found, and saves it as a GuitarSuite preset file: in the
// default folder (the app's GuitarSuite presets folder until the user chooses another, which
// is remembered), or in any file the user picks.
class ToneAnalyzerWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* trackModel READ trackModel WRITE setTrackModel NOTIFY trackModelChanged)
    Q_PROPERTY(int clipId READ clipId WRITE setClipId NOTIFY clipChanged)
    Q_PROPERTY(QString clipLabel READ clipLabel NOTIFY clipChanged)
    // Empty when the clip can be analysed, otherwise why not.
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY clipChanged)
    // What the clip plays: 0 found by the analyzer, 1 chords, 2 single notes. For the next analysis.
    Q_PROPERTY(int playing READ playing WRITE setPlaying NOTIFY playingChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY resultChanged)
    Q_PROPERTY(QString playingName READ playingName NOTIFY resultChanged)      // "Chords" / "Single notes"
    Q_PROPERTY(QString gainName READ gainName NOTIFY resultChanged)            // "High Gain + Overdrive"
    Q_PROPERTY(QString cabinetName READ cabinetName NOTIFY resultChanged)
    Q_PROPERTY(QString roomName READ roomName NOTIFY resultChanged)            // "Dry" / "Reverb" / "Delay and reverb"
    // How far the found tone's colour is from the clip's, in dB (100 Hz to 6.4 kHz), and in words.
    Q_PROPERTY(double balanceErrorDb READ balanceErrorDb NOTIFY resultChanged)
    Q_PROPERTY(QString closeness READ closeness NOTIFY resultChanged)
    // The colour band by band: {hz, want, got} in dB against each tone's own mean.
    Q_PROPERTY(QVariantList bands READ bands NOTIFY resultChanged)
    // The pedalboard: {name, settings} per pedal, in order.
    Q_PROPERTY(QVariantList pedals READ pedals NOTIFY resultChanged)
    // Where savePreset() saves: the folder set here (tests set their own; not remembered), else
    // the user's default folder (remembered, see setDefaultFolder), else the app data folder's
    // "GuitarSuite".
    Q_PROPERTY(QString presetFolder READ presetFolder WRITE setPresetFolder NOTIFY presetFolderChanged)
    Q_PROPERTY(QUrl presetFolderUrl READ presetFolderUrl NOTIFY presetFolderChanged)
    // The user chose a default folder of their own.
    Q_PROPERTY(bool hasOwnDefaultFolder READ hasOwnDefaultFolder NOTIFY presetFolderChanged)
    // The file the last savePreset() wrote (empty before).
    Q_PROPERTY(QUrl savedPreset READ savedPreset NOTIFY savedPresetChanged)

public:
    explicit ToneAnalyzerWrapper(QObject* parent = nullptr);
    ~ToneAnalyzerWrapper() override;

    QObject* trackModel() const;
    void setTrackModel(QObject* model);
    int clipId() const { return m_clipId; }
    void setClipId(int id);
    QString clipLabel() const;
    QString unavailableReason() const;
    int playing() const { return m_playing; }
    void setPlaying(int playing);
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString message() const { return m_message; }
    bool hasResult() const { return m_result.has_value(); }
    QString playingName() const;
    QString gainName() const;
    QString cabinetName() const;
    QString roomName() const;
    double balanceErrorDb() const;
    QString closeness() const;
    QVariantList bands() const;
    QVariantList pedals() const;
    QString presetFolder() const;
    void setPresetFolder(const QString& folder);
    QUrl presetFolderUrl() const;
    bool hasOwnDefaultFolder() const;
    QUrl savedPreset() const { return m_savedPreset; }

    // Starts the analysis of the clip (false, with a message, when it cannot).
    Q_INVOKABLE bool analyse();
    Q_INVOKABLE void cancel();
    // A name for the preset, from the clip's.
    Q_INVOKABLE QString suggestedName() const;
    // The found pedalboard as "<name>.gspreset" in the presets folder; false with a message (no
    // name, a name already taken, the folder not writable).
    Q_INVOKABLE bool savePreset(const QString& name);
    // The same into the file the user picked (".gspreset" added when it has no ending; a file
    // already there is replaced: the file chooser asked). makeDefault: its folder becomes the
    // default folder from now on.
    Q_INVOKABLE bool savePresetTo(const QUrl& file, bool makeDefault);
    // The file savePreset(name) would write, to start the file chooser on.
    Q_INVOKABLE QUrl fileFor(const QString& name) const;
    // The default folder from now on, remembered; an empty url: the app's own folder again.
    Q_INVOKABLE void setDefaultFolder(const QUrl& folder);

    // For tests: the result as the backend gives it.
    const std::optional<analysis::ToneMatch>& result() const { return m_result; }

signals:
    void trackModelChanged();
    void clipChanged();
    void playingChanged();
    void stateChanged();
    void progressChanged();
    void resultChanged();
    void presetFolderChanged();
    void savedPresetChanged();
    void finished(bool ok);

private:
    const struct AudioClip* clip() const;
    void setMessage(const QString& message);
    bool write(const QString& path, const QString& name);

    QPointer<TrackListWrapper> m_model;
    int m_clipId = -1;
    int m_playing = 0;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_message;
    QString m_presetFolder;
    QUrl m_savedPreset;
    std::optional<analysis::ToneMatch> m_result;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    int m_run = 0;                                         // which analysis a finished one was
};
