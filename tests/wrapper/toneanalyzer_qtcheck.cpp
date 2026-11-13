#include "testkit.h"
#include "analysis/dryguitar.h"
#include "instruments/guitar/presets/guitarsuitepreset.h"
#include "wrapper/analysis/toneanalyzerwrap/toneanalyzerwrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>

// The guitar tone analyzer's bridge (restMap G19): the selected clip in, a GuitarSuite preset file out.
namespace {

struct Storage {
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-toneanalyzer-XXXXXX")};
    Storage() {
        if (!QCoreApplication::instance()) {
            static int argc = 1; static char name[] = "toneanalyzertests"; static char* argv[]{name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerToneAnalyzerTests");
        QCoreApplication::setApplicationName("Tests");
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    }
};

void tick(int ms = 20) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }

bool settle(ToneAnalyzerWrapper& w)
{
    for (int i = 0; i < 6000 && w.busy(); ++i) tick(20);
    return !w.busy();
}

// The built-in dry guitar's chords, squared off a little (a tone with some gain), as a recorded clip's audio.
std::shared_ptr<AudioData> guitar(const char* name = "riff take.wav")
{
    auto audio = std::make_shared<AudioData>();
    audio->path = name;
    audio->sampleRate = 48000;
    auto sound = analysis::dryGuitarChords(48000.0);
    for (float& v : sound) v = 0.3f * std::tanh(40.0f * v);
    audio->channels = {std::move(sound)};
    audio->frames = std::int64_t(audio->channels[0].size());
    audio->buildWaveform();
    return audio;
}

} // namespace

TEST(qttoneanalyzer, the_selected_clip_gives_a_pedalboard_saved_as_a_guitarsuite_preset)
{
    Storage storage;
    TrackListWrapper tracks;
    tracks.setSampleRate(48000);
    const int audioTrack = tracks.addAudioTrack();
    CHECK(tracks.attachTake(audioTrack, 0, guitar(), false));
    const int clipId = tracks.project().findTrack(audioTrack)->audioClips.at(0).id;

    ToneAnalyzerWrapper w;
    CHECK(w.unavailableReason() == "No project is open.");
    w.setTrackModel(&tracks);
    CHECK(w.unavailableReason().contains("Select an audio clip"));
    CHECK(!w.analyse() && w.message().contains("Select an audio clip"));
    CHECK(!w.savePreset("x") && w.message().contains("Analyse"));
    w.setClipId(clipId);
    CHECK(w.unavailableReason().isEmpty() && !w.clipLabel().isEmpty());
    CHECK(!w.hasResult() && w.pedals().isEmpty() && w.bands().isEmpty() && w.gainName().isEmpty());
    w.setPlaying(7);
    CHECK(w.playing() == 2);
    w.setPlaying(0);

    int finishedOk = 0;
    QObject::connect(&w, &ToneAnalyzerWrapper::finished, [&](bool ok) { finishedOk += ok ? 1 : 0; });
    CHECK(w.analyse());
    CHECK(w.busy() && !w.analyse());                             // one at a time
    CHECK(settle(w));
    CHECK(finishedOk == 1 && w.hasResult() && w.message().isEmpty());
    CHECK(std::abs(w.progress() - 1.0) < 1e-9);
    CHECK(w.playingName() == "Chords");
    CHECK(!w.gainName().isEmpty() && !w.cabinetName().isEmpty() && !w.roomName().isEmpty() && !w.closeness().isEmpty());
    CHECK(w.balanceErrorDb() >= 0.0 && w.balanceErrorDb() < 6.0);
    const auto bands = w.bands();
    CHECK(bands.size() == 19);                                   // 100 Hz .. 6.3 kHz
    double wantSum = 0.0, gotSum = 0.0;
    for (const auto& band : bands) {
        const auto m = band.toMap();
        CHECK(m["hz"].toDouble() > 95.0 && m["hz"].toDouble() < 6500.0);
        wantSum += m["want"].toDouble();
        gotSum += m["got"].toDouble();
    }
    CHECK(std::abs(wantSum) < 1e-6 && std::abs(gotSum) < 1e-6);  // each against its own mean
    const auto pedals = w.pedals();
    CHECK(pedals.size() >= 4);
    CHECK(pedals[0].toMap()["name"].toString() == "Noise Gate");
    bool amp = false;
    for (const auto& pedal : pedals) {
        const auto m = pedal.toMap();
        CHECK(!m["name"].toString().isEmpty());
        if (m["name"].toString() == "Amp") { amp = true; CHECK(m["settings"].toString().contains("Drive")); }
    }
    CHECK(amp);

    // Saved under a name in the presets folder; the file reads back as a preset.
    QTemporaryDir presets;
    CHECK(presets.isValid());
    w.setPresetFolder(presets.path() + "/GuitarSuite");
    CHECK(w.suggestedName() == "riff take tone");
    CHECK(!w.savePreset("  ") && w.message().contains("name"));
    CHECK(!w.savePreset("///") && w.message().contains("name"));
    CHECK(w.savedPreset().isEmpty());
    CHECK(w.savePreset("My/ riff: tone"));
    const QString path = presets.path() + "/GuitarSuite/My riff tone.gspreset";
    CHECK(QFileInfo::exists(path) && w.savedPreset().toLocalFile() == path);
    CHECK(w.message().contains("My riff tone") && w.message().contains("Load preset"));
    GuitarSuitePresetData back;
    std::string error;
    CHECK(GuitarSuitePreset::loadFromFile(path.toStdString(), back, &error));
    CHECK(back.name == "My riff tone" && back.pedals.size() == std::size_t(pedals.size()));
    CHECK(back.pedals.front().type == "pedal.noisegate");
    // The same name again is refused, the file left as it is.
    CHECK(!w.savePreset("My riff tone") && w.message().contains("already there"));
    CHECK(w.fileFor("My riff tone").toLocalFile() == path && w.presetFolderUrl().toLocalFile() == presets.path() + "/GuitarSuite");
    CHECK(w.fileFor("  ").toLocalFile() == presets.path() + "/GuitarSuite/riff take tone.gspreset");

    // Into a file of the user's choosing: the ending added, a file there replaced, the default left alone.
    QTemporaryDir elsewhere;
    CHECK(elsewhere.isValid());
    CHECK(!w.savePresetTo(QUrl("https://example.org/x.gspreset"), false) && w.message().contains("this computer"));
    CHECK(w.savePresetTo(QUrl::fromLocalFile(elsewhere.path() + "/sub/Lead"), false));
    const QString picked = elsewhere.path() + "/sub/Lead.gspreset";
    CHECK(QFileInfo::exists(picked) && w.savedPreset().toLocalFile() == picked && w.message().contains("Lead"));
    CHECK(GuitarSuitePreset::loadFromFile(picked.toStdString(), back, &error) && back.name == "Lead");
    CHECK(w.savePresetTo(QUrl::fromLocalFile(picked), false));                 // again: replaced
    CHECK(!w.hasOwnDefaultFolder());

    // The default folder: the app's own until the user chooses one; remembered, also by a new bridge.
    w.setPresetFolder(QString());
    const QString appFolder = w.presetFolder();
    CHECK(appFolder.endsWith("/GuitarSuite") && appFolder.startsWith(storage.dir.path()));
    int folderChanges = 0;
    QObject::connect(&w, &ToneAnalyzerWrapper::presetFolderChanged, [&] { ++folderChanges; });
    CHECK(w.savePresetTo(QUrl::fromLocalFile(elsewhere.path() + "/mine/Rhythm.gspreset"), true));
    CHECK(folderChanges == 1 && w.hasOwnDefaultFolder() && w.presetFolder() == elsewhere.path() + "/mine");
    CHECK(w.savePreset("Second"));
    CHECK(QFileInfo::exists(elsewhere.path() + "/mine/Second.gspreset"));
    {
        ToneAnalyzerWrapper other;
        CHECK(other.hasOwnDefaultFolder() && other.presetFolder() == elsewhere.path() + "/mine");
    }
    w.setDefaultFolder(QUrl());
    CHECK(folderChanges == 2 && !w.hasOwnDefaultFolder() && w.presetFolder() == appFolder);
}

TEST(qttoneanalyzer, midi_short_and_cancelled_clips_say_why)
{
    Storage storage;
    TrackListWrapper tracks;
    tracks.setSampleRate(48000);
    ToneAnalyzerWrapper w;
    w.setTrackModel(&tracks);

    const int chordsTrack = tracks.addInstrumentTrackOfType("Chords");
    w.setClipId(tracks.addPatternClipAtBeat(chordsTrack, 0.0, 8.0, {}));
    CHECK(w.unavailableReason().contains("MIDI clip"));

    const int shortTrack = tracks.addAudioTrack();
    auto shortAudio = std::make_shared<AudioData>();
    shortAudio->sampleRate = 48000;
    shortAudio->channels = {std::vector<float>(48000, 0.1f)};
    shortAudio->frames = 48000;
    shortAudio->buildWaveform();
    CHECK(tracks.attachTake(shortTrack, 0, shortAudio, false));
    w.setClipId(tracks.project().findTrack(shortTrack)->audioClips.at(0).id);
    CHECK(w.unavailableReason().contains("too short"));

    const int guitarTrack = tracks.addAudioTrack();
    CHECK(tracks.attachTake(guitarTrack, 0, guitar(), false));
    w.setClipId(tracks.project().findTrack(guitarTrack)->audioClips.at(0).id);
    int finished = 0, finishedOk = 0;
    QObject::connect(&w, &ToneAnalyzerWrapper::finished, [&](bool ok) { ++finished; finishedOk += ok ? 1 : 0; });
    CHECK(w.analyse());
    w.cancel();
    CHECK(settle(w));
    CHECK(finished == 1 && finishedOk == 0 && !w.hasResult() && w.message() == "Cancelled.");
}
