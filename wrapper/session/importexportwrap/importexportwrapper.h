#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>

class TrackListWrapper;

// Import and export jobs for QML (restMap E2, F2). Import decodes the chosen files (WAV,
// FLAC, MP3) on a worker thread, keeps each as a take in the media folder, then adds them to
// the project in one undo step. One job at a time; cancel() stops it between files.
class ImportExportWrapper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* trackModel READ trackModel WRITE setTrackModel NOTIFY trackModelChanged)
    // Where imported audio is kept (the app's Recordings folder unless set).
    Q_PROPERTY(QString mediaFolder READ mediaFolder WRITE setMediaFolder NOTIFY mediaFolderChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)      // 0..1 while busy
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)         // what happens, or what happened
    Q_PROPERTY(QStringList problems READ problems NOTIFY stateChanged) // files that could not be read

public:
    explicit ImportExportWrapper(QObject* parent = nullptr);
    ~ImportExportWrapper() override;

    QObject* trackModel() const;
    void setTrackModel(QObject* model);
    QString mediaFolder() const { return m_mediaFolder; }
    void setMediaFolder(const QString& folder);
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    QStringList problems() const { return m_problems; }

    // Files (URLs or paths) at startBeat; targetTrackId < 0: a new track per file. False when a
    // job is running or nothing was given. finished() follows.
    Q_INVOKABLE bool importFiles(const QVariantList& files, double startBeat, int targetTrackId = -1);
    Q_INVOKABLE void cancel();
    // Whether a dropped or chosen file looks importable (by its name; the content decides).
    Q_INVOKABLE bool isImportable(const QUrl& file) const;

signals:
    void trackModelChanged();
    void mediaFolderChanged();
    void stateChanged();
    void finished(bool ok, int clipCount);

private:
    void setState(bool busy, double progress, const QString& status);
    QPointer<TrackListWrapper> m_model;
    QString m_mediaFolder;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_status;
    QStringList m_problems;
    std::shared_ptr<std::atomic<bool>> m_cancel = std::make_shared<std::atomic<bool>>(false);
};
