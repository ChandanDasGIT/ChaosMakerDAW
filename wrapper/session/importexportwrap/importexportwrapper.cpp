#include "importexportwrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "audiofile/audioimport.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>
#include <QThreadPool>

#include <filesystem>

ImportExportWrapper::ImportExportWrapper(QObject* parent)
    : QObject(parent),
      m_mediaFolder(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Recordings"))
{
}

ImportExportWrapper::~ImportExportWrapper()
{
    m_cancel->store(true);
    QThreadPool::globalInstance()->waitForDone();
}

QObject* ImportExportWrapper::trackModel() const { return m_model; }

void ImportExportWrapper::setTrackModel(QObject* model)
{
    auto* tracks = qobject_cast<TrackListWrapper*>(model);
    if (tracks == m_model) return;
    m_model = tracks;
    emit trackModelChanged();
}

void ImportExportWrapper::setMediaFolder(const QString& folder)
{
    if (folder == m_mediaFolder) return;
    m_mediaFolder = folder;
    emit mediaFolderChanged();
}

void ImportExportWrapper::setState(bool busy, double progress, const QString& status)
{
    m_busy = busy;
    m_progress = progress;
    m_status = status;
    emit stateChanged();
}

void ImportExportWrapper::cancel()
{
    if (m_busy) m_cancel->store(true);
}

bool ImportExportWrapper::isImportable(const QUrl& file) const
{
    const QString suffix = QFileInfo(file.isLocalFile() ? file.toLocalFile() : file.toString()).suffix().toLower();
    return suffix == QLatin1String("wav") || suffix == QLatin1String("wave") || suffix == QLatin1String("flac")
        || suffix == QLatin1String("mp3");
}

bool ImportExportWrapper::importFiles(const QVariantList& files, double startBeat, int targetTrackId)
{
    if (m_busy || !m_model || files.isEmpty()) return false;
    std::vector<std::string> paths;
    for (const auto& f : files) {
        const QUrl url = f.canConvert<QUrl>() ? f.toUrl() : QUrl();
        const QString path = url.isLocalFile() ? url.toLocalFile() : f.toString();
        if (!path.isEmpty()) paths.push_back(path.toStdString());
    }
    if (paths.empty()) return false;
    m_problems.clear();
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    setState(true, 0.0, QStringLiteral("Importing %1 file(s)…").arg(paths.size()));
    const auto cancelled = m_cancel;
    const std::string folder = m_mediaFolder.toStdString();
    QPointer<ImportExportWrapper> self(this);
    QThreadPool::globalInstance()->start([self, paths, folder, cancelled, startBeat, targetTrackId] {
        std::vector<std::shared_ptr<const AudioData>> done;
        QStringList problems;
        for (std::size_t i = 0; i < paths.size() && !cancelled->load(); ++i) {
            const auto name = QString::fromStdString(std::filesystem::path(paths[i]).filename().string());
            if (self) QMetaObject::invokeMethod(self, [self, i, total = paths.size(), name] {
                if (self) self->setState(true, double(i) / double(total), QStringLiteral("Reading %1…").arg(name));
            }, Qt::QueuedConnection);
            try {
                const auto audio = audioimport::read(paths[i]);
                if (cancelled->load()) break;
                done.push_back(audioimport::saveAsTake(*audio, folder, std::filesystem::path(paths[i]).filename().string()));
            } catch (const std::exception& e) {
                problems << QString::fromStdString(e.what());
            }
        }
        const bool wasCancelled = cancelled->load();
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, done, problems, wasCancelled, startBeat, targetTrackId] {
            if (!self) return;
            self->m_problems = problems;
            int clips = 0;
            if (!wasCancelled && !done.empty() && self->m_model)
                clips = int(self->m_model->importAudio(done, startBeat, targetTrackId).size());
            QString status = wasCancelled ? QStringLiteral("Import cancelled.")
                           : clips > 0 ? QStringLiteral("Imported %1 file(s).").arg(clips)
                                       : QStringLiteral("Nothing was imported.");
            if (!problems.isEmpty()) status += QStringLiteral(" %1 could not be read.").arg(problems.size());
            self->setState(false, 1.0, status);
            emit self->finished(!wasCancelled && clips > 0, clips);
        }, Qt::QueuedConnection);
    });
    return true;
}
