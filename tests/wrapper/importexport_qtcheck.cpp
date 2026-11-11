#include "testkit.h"
#include "wrapper/session/importexportwrap/importexportwrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "audiodevice/audiodevice.h"
#include "audiofile/audioimport.h"
#include "audiofile/mp3reader.h"
#include "audiofile/wavwriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <cmath>

namespace {
void application()
{
    if (QCoreApplication::instance()) return;
    static int argc = 1; static char name[] = "importexporttests"; static char* argv[]{name, nullptr};
    static QCoreApplication app(argc, argv);
}
bool waitFor(ImportExportWrapper& job, int ms = 10000)
{
    QEventLoop loop;
    bool done = false;
    QObject::connect(&job, &ImportExportWrapper::finished, &loop, [&] { done = true; loop.quit(); });
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    if (job.busy()) loop.exec();
    return done || !job.busy();
}
// A device that never plays: the export renders offline on its own.
class SilentDevice final : public AudioDevice {
public:
    bool open(AudioCallback* c, int frames) override { c->deviceChanged(48000, frames); return true; }
    void close() override {}
    double sampleRate() const override { return 48000; }
    DeviceLatency latency() const override { return {128, 128, 128}; }
    bool setBufferSize(int) override { return true; }
    std::vector<std::string> inputNames() const override { return {}; }
    std::vector<std::string> outputNames() const override { return {"test:left", "test:right"}; }
    bool connectInput(const std::string&) override { return true; }
    int connectInputChannel(const std::string&) override { return 0; }
    bool connectOutput(const std::string&, const std::string&) override { return true; }
    std::string lastError() const override { return {}; }
};
// Settings and app data in a throwaway folder (never the real app's).
void isolate(const QString& path)
{
    QCoreApplication::setOrganizationName("ChaosMakerImportExportTests");
    QCoreApplication::setApplicationName("Tests");
    qputenv("XDG_DATA_HOME", path.toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path);
}
bool waitForExport(EngineController& controller, bool& ok, QString& message, int ms = 20000)
{
    QEventLoop loop;
    bool done = false;
    QObject::connect(&controller, &EngineController::exportFinished, &loop, [&](bool good, const QString& text) {
        done = true; ok = good; message = text; loop.quit();
    });
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    if (controller.exporting()) loop.exec();
    return done;
}
std::vector<std::vector<float>> tone(int channels, int frames)
{
    std::vector<std::vector<float>> out(static_cast<std::size_t>(channels), std::vector<float>(static_cast<std::size_t>(frames)));
    for (auto& c : out) for (int i = 0; i < frames; ++i) c[std::size_t(i)] = float(0.3 * std::sin(i * 0.05));
    return out;
}
}

TEST(qtimportexport, files_import_in_the_background_as_new_tracks_and_bad_ones_are_named)
{
    application();
    QTemporaryDir dir(QStringLiteral("/tmp/chaosmaker-importexport-XXXXXX"));
    CHECK(dir.isValid());
    audiofile::writeWav((dir.path() + "/Guitar.wav").toStdString(), tone(2, 48000), 48000, audiofile::WavFormat::Int24);
    audiofile::writeWav((dir.path() + "/Vox.wav").toStdString(), tone(1, 24000), 44100, audiofile::WavFormat::Int16);
    { QFile junk(dir.path() + "/junk.wav"); CHECK(junk.open(QIODevice::WriteOnly)); junk.write("not audio at all"); }

    TrackListWrapper tracks;
    tracks.setSampleRate(48000);
    ImportExportWrapper job;
    job.setTrackModel(&tracks);
    job.setMediaFolder(dir.path() + "/media");
    CHECK(job.isImportable(QUrl::fromLocalFile("/x/a.FLAC")) && job.isImportable(QUrl::fromLocalFile("/x/b.mp3")));
    CHECK(!job.isImportable(QUrl::fromLocalFile("/x/c.txt")));
    CHECK(job.importFiles({QUrl::fromLocalFile(dir.path() + "/Guitar.wav"), QUrl::fromLocalFile(dir.path() + "/junk.wav"),
                           QUrl::fromLocalFile(dir.path() + "/Vox.wav")}, 2.0));
    CHECK(job.busy());
    CHECK(!job.importFiles({QUrl::fromLocalFile(dir.path() + "/Vox.wav")}, 0.0));   // one job at a time
    CHECK(waitFor(job));
    CHECK(!job.busy());
    CHECK(tracks.count() == 2);
    CHECK(job.problems().size() == 1 && job.problems()[0].contains("junk.wav"));
    CHECK(job.status().contains("Imported 2"));
    CHECK(QDir(dir.path() + "/media").entryList(QDir::Files).size() == 2);   // kept as takes
    CHECK(tracks.data(tracks.index(0), TrackListWrapper::NameRole).toString() == "Guitar");
    CHECK(tracks.clips().size() == 2);
    CHECK(tracks.undo());                                                     // one step
    CHECK(tracks.count() == 0);
    CHECK(tracks.redo());
    CHECK(tracks.count() == 2);

    // Onto an existing track.
    const int target = tracks.trackIdAt(0);
    CHECK(job.importFiles({QUrl::fromLocalFile(dir.path() + "/Vox.wav")}, 8.0, target));
    CHECK(waitFor(job));
    CHECK(tracks.count() == 2 && tracks.project().findTrack(target)->audioClips.size() == 2);
}

TEST(qtimportexport, the_song_exports_to_wav_and_mp3_in_the_background)
{
    application();
    QTemporaryDir dir(QStringLiteral("/tmp/chaosmaker-export-XXXXXX"));
    CHECK(dir.isValid());
    isolate(dir.path());
    audiofile::writeWav((dir.path() + "/Guitar.wav").toStdString(), tone(2, 48000), 48000, audiofile::WavFormat::Float32);

    EngineController controller(std::make_unique<SilentDevice>(), false);
    TrackListWrapper tracks;
    tracks.setSampleRate(48000);
    controller.attachProject(&tracks);
    controller.setBpm(120.0);
    const QUrl wav = QUrl::fromLocalFile(dir.path() + "/out.wav");
    CHECK(controller.songEndBeats() == 0.0);
    CHECK(!controller.startExport(wav, 0, 0, "wav24", 320, 0, false, 0));      // nothing to export yet

    ImportExportWrapper job;
    job.setTrackModel(&tracks);
    job.setMediaFolder(dir.path() + "/media");
    CHECK(job.importFiles({QUrl::fromLocalFile(dir.path() + "/Guitar.wav")}, 0.0));
    CHECK(waitFor(job));
    CHECK(std::abs(controller.songEndBeats() - 2.0) < 1e-6);                    // 1 s at 120 bpm

    // The whole song plus a 0.5 s tail, as 24-bit WAV.
    CHECK(!controller.startExport(wav, 0, 0, "flac", 320, 0, false, 0));        // not a format
    CHECK(controller.startExport(wav, 0, 0, "wav24", 320, 0.5, false, 0));
    CHECK(controller.exporting());
    CHECK(!controller.startExport(wav, 0, 0, "wav24", 320, 0.5, false, 0));     // one at a time
    bool ok = false;
    QString message;
    CHECK(waitForExport(controller, ok, message));
    CHECK(ok && message.contains("out.wav") && !controller.exporting());
    const auto rendered = audioimport::read(wav.toLocalFile().toStdString());
    CHECK(rendered->sampleRate == 48000.0 && rendered->frames == 48000 + 24000);
    float peak = 0;
    for (std::size_t i = 0; i < 48000; ++i) peak = std::max(peak, std::abs(rendered->channels[0][i]));
    CHECK(peak > 0.1f);
    CHECK(std::abs(rendered->channels[0][60000]) < 1e-5f);                      // the tail is silent here

    // A time selection, normalised to -1 dBFS, as MP3.
    const QUrl mp3 = QUrl::fromLocalFile(dir.path() + "/part.mp3");
    CHECK(controller.startExport(mp3, 0.5, 1.5, "mp3", 192, 0, true, -1.0));
    CHECK(waitForExport(controller, ok, message));
    CHECK(ok);
    const auto decoded = audiofile::readMp3(mp3.toLocalFile().toStdString());
    CHECK(decoded.bitrateKbps == 192 && decoded.audio->sampleRate == 48000.0);
    CHECK(decoded.audio->frames == 24000);                                      // beats 0.5 .. 1.5 exactly
    peak = 0;
    for (const auto& c : decoded.audio->channels) for (float v : c) peak = std::max(peak, std::abs(v));
    CHECK(std::abs(peak - std::pow(10.0f, -1.0f / 20.0f)) < 0.03f);

    // Cancel: no file is reported as written.
    CHECK(controller.startExport(QUrl::fromLocalFile(dir.path() + "/cancel.wav"), 0, 0, "wav16", 320, 10.0, false, 0));
    controller.cancelExport();
    CHECK(waitForExport(controller, ok, message));
    CHECK(!ok && message.contains("cancelled"));
    controller.shutdownSession();
}
