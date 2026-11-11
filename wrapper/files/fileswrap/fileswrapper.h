#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Files and folders for QML (the `Files` singleton), used by the app's own file chooser
// (frontend/dialogs/FilePicker.qml). QML never builds paths itself ("file://" + "/"), it asks
// here. Paths use "/" between folders; drive letters ("C:/") work too.
class FilesWrapper : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Files)
    QML_SINGLETON

public:
    explicit FilesWrapper(QObject* parent = nullptr);

    Q_INVOKABLE QString toPath(const QUrl& url) const;
    Q_INVOKABLE QUrl toUrl(const QString& path) const;
    // For showing: the platform's own separators ("C:\Music").
    Q_INVOKABLE QString displayPath(const QUrl& url) const;
    Q_INVOKABLE QUrl child(const QUrl& folder, const QString& name) const;
    Q_INVOKABLE bool isRoot(const QUrl& folder) const;
    Q_INVOKABLE QUrl parentOf(const QUrl& folder) const;   // a root: itself
    // The path bar: the root, then each folder down to this one: [{ name, url }].
    Q_INVOKABLE QVariantList crumbs(const QUrl& folder) const;
    // Places beyond home: [{ name, url }]. Linux: other disks and USB sticks (/mnt, /media,
    // /run/media); Windows: every drive.
    Q_INVOKABLE QVariantList drives() const;
    Q_INVOKABLE QString homeFolder() const;

    Q_INVOKABLE bool exists(const QUrl& url) const;
    Q_INVOKABLE bool isFolder(const QUrl& url) const;
    // A file's folder; a folder: itself. Empty or missing: an empty url.
    Q_INVOKABLE QUrl folderOf(const QUrl& url) const;
    Q_INVOKABLE QString fileName(const QUrl& url) const;
    // "song" + "wav" = "song.wav"; a name that already ends in ".wav" (any case) is kept.
    Q_INVOKABLE QString withSuffix(const QString& name, const QString& suffix) const;
    // A name filter's patterns: "Audio files (*.wav *.mp3)" = ["*.wav", "*.mp3"];
    // "*.wav" alone = ["*.wav"]. "All files (*)" = ["*"].
    Q_INVOKABLE QStringList filterPatterns(const QString& filter) const;
    // Its label: "Audio files (*.wav *.mp3)" = "Audio files".
    Q_INVOKABLE QString filterLabel(const QString& filter) const;
    // The file name matches one of the patterns (ignoring case).
    Q_INVOKABLE bool matchesFilter(const QString& name, const QStringList& patterns) const;
};
