#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>

class EngineController;

// The export dialog (restMap F2): file names, starting and cancelling an export, its progress.
// While it runs, live audio is silent and paused (EngineController::suspendForOfflineRender).
class ExportDialogWrap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportChanged)

public:
    explicit ExportDialogWrap(QObject* parent = nullptr);
    explicit ExportDialogWrap(EngineController* engine, QObject* parent = nullptr);

    bool exporting() const { return m_exporting; }
    double exportProgress() const { return m_exportProgress; }

    // Renders startBeat..endBeat (endBeat <= startBeat: the whole song, from its start to the end
    // of its last clip) plus tailSeconds into `file` on a worker thread; exportFinished follows.
    // format: "wav16" (dithered), "wav24", "wav32f" or "mp3" (mp3Kbps 128..320). normalise: 0 off,
    // else the peak in dBFS (e.g. -1, 0) as normalisePeakDb with normalise true. False (with
    // exportFinished not sent) while recording, exporting or for an empty song / bad range.
    Q_INVOKABLE bool startExport(const QUrl& file, double startBeat, double endBeat, const QString& format,
                                 int mp3Kbps, double tailSeconds, bool normalise, double normalisePeakDb);
    Q_INVOKABLE void cancelExport();
    // The song's length in beats (to the end of its last clip); 0 when empty.
    Q_INVOKABLE double songEndBeats() const;

    // The file offered first: the project's name and folder (else "Untitled" in Music).
    Q_INVOKABLE QUrl defaultFile(const QUrl& projectFile, const QString& extension) const;
    Q_INVOKABLE QUrl folder(const QUrl& projectFile) const;
    Q_INVOKABLE QUrl withExtension(const QUrl& file, const QString& extension) const;
    Q_INVOKABLE QString fileLabel(const QUrl& file) const;
    // Why startExport() said no, for the whole song (selection false) or a selection.
    Q_INVOKABLE QString cannotStartMessage(bool selection) const;

signals:
    void exportChanged();
    void exportFinished(bool ok, const QString& message);

private:
    EngineController* engine() const;

    QPointer<EngineController> m_engine;
    bool m_exporting = false;
    double m_exportProgress = 0.0;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};
