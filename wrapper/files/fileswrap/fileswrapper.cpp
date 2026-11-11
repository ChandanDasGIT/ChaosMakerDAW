#include "fileswrapper.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStorageInfo>

namespace {
QVariantMap place(const QString& name, const QString& path)
{
    return {{QStringLiteral("name"), name}, {QStringLiteral("url"), QUrl::fromLocalFile(path)}};
}
}

FilesWrapper::FilesWrapper(QObject* parent) : QObject(parent) {}

QString FilesWrapper::toPath(const QUrl& url) const
{
    if (url.isEmpty()) return {};
    return url.isLocalFile() ? QDir::cleanPath(url.toLocalFile()) : QDir::cleanPath(url.toString());
}

QUrl FilesWrapper::toUrl(const QString& path) const
{
    return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(QDir::cleanPath(path));
}

QString FilesWrapper::displayPath(const QUrl& url) const { return QDir::toNativeSeparators(toPath(url)); }

QUrl FilesWrapper::child(const QUrl& folder, const QString& name) const
{
    return QUrl::fromLocalFile(QDir::cleanPath(QDir(toPath(folder)).filePath(name)));
}

bool FilesWrapper::isRoot(const QUrl& folder) const { return QDir(toPath(folder)).isRoot(); }

QUrl FilesWrapper::parentOf(const QUrl& folder) const
{
    QDir dir(toPath(folder));
    if (!dir.isRoot()) dir.cdUp();
    return QUrl::fromLocalFile(dir.absolutePath());
}

QVariantList FilesWrapper::crumbs(const QUrl& folder) const
{
    QVariantList out;
    QDir dir(toPath(folder));
    for (;;) {
        const QString path = dir.absolutePath();
        // A root shows as "/" or "C:".
        const QString name = dir.isRoot() ? (path == QStringLiteral("/") ? path : path.chopped(path.endsWith(u'/') ? 1 : 0))
                                          : dir.dirName();
        out.prepend(place(name, path));
        if (dir.isRoot() || !dir.cdUp()) break;
    }
    return out;
}

QVariantList FilesWrapper::drives() const
{
    QVariantList out;
    for (const QStorageInfo& volume : QStorageInfo::mountedVolumes()) {
        const QString root = volume.rootPath();
        if (!volume.isValid() || !volume.isReady()) continue;
#if defined(Q_OS_WIN)
        const QString letter = root.left(2);   // every drive: "Data (D:)"
        const QString label = volume.name();
        out.append(place(label.isEmpty() ? letter : QStringLiteral("%1 (%2)").arg(label, letter), root));
#else
        // The system's own mounts are left out: other disks and USB sticks only.
        if (!root.startsWith(QStringLiteral("/mnt/")) && !root.startsWith(QStringLiteral("/media/"))
            && !root.startsWith(QStringLiteral("/run/media/")))
            continue;
        QString name = volume.displayName();
        if (name.isEmpty() || name == root) name = root.section(u'/', -1);
        out.append(place(name, root));
#endif
    }
    return out;
}

QString FilesWrapper::homeFolder() const { return QDir::homePath(); }

bool FilesWrapper::exists(const QUrl& url) const { return !url.isEmpty() && QFileInfo::exists(toPath(url)); }

bool FilesWrapper::isFolder(const QUrl& url) const { return !url.isEmpty() && QFileInfo(toPath(url)).isDir(); }

QUrl FilesWrapper::folderOf(const QUrl& url) const
{
    if (url.isEmpty()) return {};
    const QFileInfo info(toPath(url));
    if (info.isDir()) return QUrl::fromLocalFile(info.absoluteFilePath());
    const QDir folder = info.absoluteDir();
    return folder.exists() ? QUrl::fromLocalFile(folder.absolutePath()) : QUrl();
}

QString FilesWrapper::fileName(const QUrl& url) const { return QFileInfo(toPath(url)).fileName(); }

QString FilesWrapper::withSuffix(const QString& name, const QString& suffix) const
{
    const QString clean = suffix.startsWith(u'.') ? suffix.mid(1) : suffix;
    if (name.isEmpty() || clean.isEmpty() || name.endsWith(u'.' + clean, Qt::CaseInsensitive)) return name;
    return name + u'.' + clean;
}

QStringList FilesWrapper::filterPatterns(const QString& filter) const
{
    static const QRegularExpression inBrackets(QStringLiteral("\\(([^)]*)\\)"));
    const auto match = inBrackets.match(filter);
    const QString list = match.hasMatch() ? match.captured(1) : filter;
    return list.split(u' ', Qt::SkipEmptyParts);
}

QString FilesWrapper::filterLabel(const QString& filter) const
{
    const int bracket = filter.indexOf(u'(');
    return (bracket > 0 ? filter.left(bracket) : filter).trimmed();
}

bool FilesWrapper::matchesFilter(const QString& name, const QStringList& patterns) const
{
    for (const QString& pattern : patterns) {
        const auto re = QRegularExpression::fromWildcard(pattern, Qt::CaseInsensitive);
        if (re.match(name).hasMatch()) return true;
    }
    return false;
}
