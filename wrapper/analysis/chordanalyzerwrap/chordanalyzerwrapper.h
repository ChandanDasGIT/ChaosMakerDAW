#pragma once

#include "analysis/chordfinder.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

class TrackListWrapper;

// The chord analyzer's bridge (restMap G21), for Tools > Chord analyzer…: analyses the selected
// audio clip, a whole song (backend/analysis, on a worker thread with progress and cancel),
// keeps the result, and puts it on a Chords track: the song's most played chords on the eight
// strips and its key, and, when asked, a clip under the analysed audio that presses the strips
// in time with the song. Setting the project's BPM and meter is the page's (EngineController);
// the clip is placed by the tempo the project has when it is made.
class ChordAnalyzerWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* trackModel READ trackModel WRITE setTrackModel NOTIFY trackModelChanged)
    Q_PROPERTY(int clipId READ clipId WRITE setClipId NOTIFY clipChanged)
    Q_PROPERTY(QString clipLabel READ clipLabel NOTIFY clipChanged)
    // Empty when the clip can be analysed, otherwise why not.
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY clipChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY resultChanged)
    // Sevenths (7, maj7, m7) named as well as major and minor; changing it reads the chords
    // again without a new analysis.
    Q_PROPERTY(bool sevenths READ sevenths WRITE setSevenths NOTIFY seventhsChanged)
    Q_PROPERTY(QString keyName READ keyName NOTIFY resultChanged)
    Q_PROPERTY(bool hasBeat READ hasBeat NOTIFY resultChanged)
    Q_PROPERTY(double bpm READ bpm NOTIFY resultChanged)
    Q_PROPERTY(int numerator READ numerator NOTIFY resultChanged)
    Q_PROPERTY(int denominator READ denominator NOTIFY resultChanged)
    Q_PROPERTY(double tuningCents READ tuningCents NOTIFY resultChanged)
    Q_PROPERTY(double seconds READ seconds NOTIFY resultChanged)
    // The chords in time order: {name ("" = no chord), from, to (seconds), beats, sure (0..1)}.
    Q_PROPERTY(QVariantList chords READ chords NOTIFY resultChanged)
    // The chord names, the most played first.
    Q_PROPERTY(QStringList commonChords READ commonChords NOTIFY resultChanged)

public:
    explicit ChordAnalyzerWrapper(QObject* parent = nullptr);
    ~ChordAnalyzerWrapper() override;

    QObject* trackModel() const;
    void setTrackModel(QObject* model);
    int clipId() const { return m_clipId; }
    void setClipId(int id);
    QString clipLabel() const;
    QString unavailableReason() const;
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString message() const { return m_message; }
    bool hasResult() const { return m_result.has_value(); }
    bool sevenths() const { return m_sevenths; }
    void setSevenths(bool on);
    QString keyName() const;
    bool hasBeat() const;
    double bpm() const;
    int numerator() const;
    int denominator() const;
    double tuningCents() const;
    double seconds() const;
    QVariantList chords() const;
    QStringList commonChords() const;

    // Starts the analysis of the clip (false, with a message, when it cannot).
    Q_INVOKABLE bool analyse();
    Q_INVOKABLE void cancel();
    // Where the chords can go: {id, name}; -1 a new Chords track, then the project's Chords tracks.
    Q_INVOKABLE QVariantList targets() const;
    // The (up to eight) most played chords onto `target`'s strips, with the key; `withClip`: also
    // a clip under the analysed audio that presses the strips as the song goes. The track's id,
    // or -1 with a message.
    Q_INVOKABLE int putOnTrack(int target, bool withClip);
    // The chords as text, a line per stretch of the song ("0:00  Gm  Bb  Cm  Gm").
    Q_INVOKABLE QString chartText() const;
    Q_INVOKABLE void copyChart() const;                   // chartText() to the clipboard

    // For tests: the result as the backend gives it.
    const std::optional<analysis::ChordAnalysis>& result() const { return m_result; }

signals:
    void trackModelChanged();
    void clipChanged();
    void stateChanged();
    void progressChanged();
    void resultChanged();
    void seventhsChanged();
    void finished(bool ok);

private:
    const struct AudioClip* clip() const;
    void setMessage(const QString& message);

    QPointer<TrackListWrapper> m_model;
    int m_clipId = -1;
    bool m_busy = false;
    bool m_sevenths = false;
    double m_progress = 0.0;
    QString m_message;
    std::optional<analysis::ChordAnalysis> m_result;
    std::int64_t m_analysedStart = 0;                      // the clip's start (project samples) when analysed
    std::shared_ptr<std::atomic<bool>> m_cancel;
    int m_run = 0;                                         // which analysis a finished one was
};
