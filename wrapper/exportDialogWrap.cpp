#include "exportDialogWrap.h"

#include "exportDialog/exportdialog.h"
#include "wrapper/common/engine/engineController.h"

#include <QMetaObject>
#include <QStandardPaths>
#include <QThreadPool>

#include <algorithm>
#include <cmath>

ExportDialogWrap::ExportDialogWrap(QObject* parent) : QObject(parent) {}

ExportDialogWrap::ExportDialogWrap(EngineController* engine, QObject* parent) : QObject(parent), m_engine(engine) {}

EngineController* ExportDialogWrap::engine() const
{
    return m_engine ? m_engine.data() : EngineController::instance();
}

double ExportDialogWrap::songEndBeats() const
{
    const auto* controller = engine();
    const auto* model = controller ? controller->projectModel() : nullptr;
    if (!model) return 0.0;
    double end = 0.0;
    for (const auto& track : model->project().tracks())
        for (const auto& clip : track->audioClips)
            end = std::max(end, model->project().tempo().beatAt(clip.start + clip.length));
    return end;
}

bool ExportDialogWrap::startExport(const QUrl& file, double startBeat, double endBeat, const QString& format,
                                   int mp3Kbps, double tailSeconds, bool normalise, double normalisePeakDb)
{
    auto* controller = engine();
    auto* model = controller ? controller->projectModel() : nullptr;
    if (!model || m_exporting || controller->takeBusy() || controller->recording() || !file.isLocalFile()) return false;
    if (!exportdialog::isFormat(format.toStdString())) return false;
    if (!(endBeat > startBeat)) { startBeat = 0.0; endBeat = songEndBeats(); }
    if (!std::isfinite(startBeat) || !std::isfinite(endBeat) || startBeat < 0 || endBeat <= startBeat) return false;
    const auto& tempo = model->project().tempo();
    const double rate = tempo.sampleRate() > 1000 ? tempo.sampleRate() : 48000.0;
    songrender::Options render;
    render.sampleRate = rate;
    render.start = tempo.sampleAt(startBeat);
    render.length = tempo.sampleAt(endBeat) - render.start;
    render.tail = std::llround(std::clamp(std::isfinite(tailSeconds) ? tailSeconds : 0.0, 0.0, 10.0) * rate);
    render.bpm = controller->bpm();
    render.masterVolume = model->project().masterVolume();
    render.masterMuted = model->project().masterMuted();
    if (render.length <= 0) return false;

    auto snapshot = controller->suspendForOfflineRender();
    m_exporting = true;
    m_exportProgress = 0.0;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    emit exportChanged();
    controller->showTransportMessage(QStringLiteral("Exporting…"));

    exportdialog::FileOptions options;
    options.path = file.toLocalFile().toStdString();
    options.format = format.toStdString();
    options.mp3Kbps = mp3Kbps;
    options.normalise = normalise;
    options.normalisePeakDb = float(normalisePeakDb);
    const auto cancel = m_cancel;
    QPointer<ExportDialogWrap> self(this);
    auto job = std::make_shared<std::unique_ptr<EngineSnapshot>>(std::move(snapshot));
    QThreadPool::globalInstance()->start([self, job, render, options, cancel] {
        const auto result = exportdialog::exportSong(
            std::move(*job), render, options,
            [self](double done) {
                if (self) QMetaObject::invokeMethod(self, [self, done] {
                    if (!self) return;
                    self->m_exportProgress = done * 0.9;
                    emit self->exportChanged();
                }, Qt::QueuedConnection);
            },
            [cancel] { return cancel->load(); });
        if (!self) return;
        const bool ok = result.ok;
        const QString message = QString::fromStdString(result.message);
        QMetaObject::invokeMethod(self, [self, ok, message] {
            if (!self) return;
            if (auto* controller = self->engine()) controller->resumeAfterOfflineRender(message);
            self->m_exporting = false;
            self->m_exportProgress = ok ? 1.0 : 0.0;
            emit self->exportChanged();
            emit self->exportFinished(ok, message);
        }, Qt::QueuedConnection);
    });
    return true;
}

void ExportDialogWrap::cancelExport()
{
    if (m_cancel) m_cancel->store(true);
}

QUrl ExportDialogWrap::folder(const QUrl& projectFile) const
{
    const auto music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation).toStdString();
    return QUrl::fromLocalFile(QString::fromStdString(
        exportdialog::folder(projectFile.toLocalFile().toStdString(), music)));
}

QUrl ExportDialogWrap::defaultFile(const QUrl& projectFile, const QString& extension) const
{
    const auto name = exportdialog::baseName(projectFile.toLocalFile().toStdString());
    return QUrl::fromLocalFile(folder(projectFile).toLocalFile() + "/" + QString::fromStdString(name) + "." + extension);
}

QUrl ExportDialogWrap::withExtension(const QUrl& file, const QString& extension) const
{
    return QUrl::fromLocalFile(QString::fromStdString(
        exportdialog::withExtension(file.toLocalFile().toStdString(), extension.toStdString())));
}

QString ExportDialogWrap::fileLabel(const QUrl& file) const
{
    return file.toLocalFile();
}

QString ExportDialogWrap::cannotStartMessage(bool selection) const
{
    return QString::fromStdString(exportdialog::cannotStartMessage(songEndBeats() <= 0 && !selection));
}
