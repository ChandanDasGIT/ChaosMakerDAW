#pragma once

#include "analysis/drumtranscriber.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>
#include <optional>

class TrackListWrapper;

// The drum pattern analyzer's bridge (restMap G20), for Tools > Drum pattern analyzer…: analyses
// the selected audio clip (backend/analysis, on a worker thread with progress and cancel), keeps
// the result, and makes from it a pattern clip on a drum track (a new Drums or Electronic Drums
// track, or one the user has) under the analysed audio, or a groove (the drum pages' grooves
// folder). Setting the project's BPM and meter is the page's (EngineController); the clip is
// placed by the tempo the project has when it is made.
class DrumAnalyzerWrapper : public QObject {
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
    Q_PROPERTY(double bpm READ bpm NOTIFY resultChanged)
    Q_PROPERTY(int numerator READ numerator NOTIFY resultChanged)
    Q_PROPERTY(int denominator READ denominator NOTIFY resultChanged)
    Q_PROPERTY(int bars READ bars NOTIFY resultChanged)
    Q_PROPERTY(int kicks READ kicks NOTIFY resultChanged)
    Q_PROPERTY(int snares READ snares NOTIFY resultChanged)
    Q_PROPERTY(int hats READ hats NOTIFY resultChanged)
    // The first two bars for the page's small grid: {row (0 kick, 1 snare, 2 hi-hat), column, velocity}.
    Q_PROPERTY(QVariantList preview READ preview NOTIFY resultChanged)
    Q_PROPERTY(int previewColumns READ previewColumns NOTIFY resultChanged)
    Q_PROPERTY(int columnsPerBeat READ columnsPerBeat NOTIFY resultChanged)
    // Where the user's grooves live; empty = the app data folder's "grooves" (tests set their own).
    Q_PROPERTY(QString grooveFolder READ grooveFolder WRITE setGrooveFolder NOTIFY grooveFolderChanged)

public:
    explicit DrumAnalyzerWrapper(QObject* parent = nullptr);
    ~DrumAnalyzerWrapper() override;

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
    double bpm() const;
    int numerator() const;
    int denominator() const;
    int bars() const;
    int kicks() const;
    int snares() const;
    int hats() const;
    QVariantList preview() const;
    int previewColumns() const;
    int columnsPerBeat() const;
    QString grooveFolder() const;
    void setGrooveFolder(const QString& folder);

    // Starts the analysis of the clip (false, with a message, when it cannot).
    Q_INVOKABLE bool analyse();
    Q_INVOKABLE void cancel();
    // The tempo doubled (2) or halved (0.5) without analysing again; false when out of range.
    Q_INVOKABLE bool retempo(double factor);
    // Where the clip can go: {id, name}; -1 a new Drums track, -2 a new Electronic Drums track,
    // then the project's drum tracks.
    Q_INVOKABLE QVariantList targets() const;
    // Lines the analysed clip up with the project's bars: it slides by less than a bar so that
    // the bar line found in it lands on a project bar line (one undo step). For right after the
    // project's BPM is set to the found one, before makeClip(). False when it could not (no
    // result, the clip gone, recording).
    Q_INVOKABLE bool alignSong();
    // The pattern as a clip on `target` (one of targets()), under the analysed audio; its id, or
    // -1 with a message.
    Q_INVOKABLE int makeClip(int target);
    // The most typical two bars as a groove named `name`; false with a message (a taken name...).
    Q_INVOKABLE bool saveGroove(const QString& name);
    Q_INVOKABLE QString suggestedGrooveName() const;

    // For tests: the result as the backend gives it.
    const std::optional<analysis::DrumAnalysis>& result() const { return m_result; }

signals:
    void trackModelChanged();
    void clipChanged();
    void stateChanged();
    void progressChanged();
    void resultChanged();
    void grooveFolderChanged();
    void finished(bool ok);

private:
    const struct AudioClip* clip() const;
    void setMessage(const QString& message);

    QPointer<TrackListWrapper> m_model;
    int m_clipId = -1;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_message;
    QString m_grooveFolder;
    std::optional<analysis::DrumAnalysis> m_result;
    std::int64_t m_analysedStart = 0;                      // the clip's start (project samples) when analysed
    double m_songMovedSeconds = 0.0;                       // by alignSong(), until the next message says so
    QString songMovedText();
    std::shared_ptr<std::atomic<bool>> m_cancel;
    int m_run = 0;                                         // which analysis a finished one was
};
