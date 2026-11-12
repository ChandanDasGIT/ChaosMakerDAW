#include "testkit.h"
#include "analysis/drumrender.h"
#include "persistence/groovestore.h"
#include "wrapper/analysis/drumanalyzerwrap/drumanalyzerwrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>

// The drum pattern analyzer's bridge (restMap G20): the selected clip in, a clip or a groove out.
namespace {

struct Storage {
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-drumanalyzer-XXXXXX")};
    Storage() {
        if (!QCoreApplication::instance()) {
            static int argc = 1; static char name[] = "drumanalyzertests"; static char* argv[]{name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerDrumAnalyzerTests");
        QCoreApplication::setApplicationName("Tests");
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    }
};

void tick(int ms = 20) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }

bool settle(DrumAnalyzerWrapper& w)
{
    for (int i = 0; i < 1500 && w.busy(); ++i) tick(20);
    return !w.busy();
}

constexpr double LoopBpm = 110.0, LoopStart = 0.4, Seconds = 16.0;

// A rock loop at 110 BPM played by the electronic kit, as a recorded clip's audio.
std::shared_ptr<AudioData> drumLoop(std::vector<drumrender::Timed>* played = nullptr)
{
    const auto hits = drumrender::timed(drumrender::rock(), LoopBpm, 6, LoopStart);
    if (played) *played = hits;
    auto audio = std::make_shared<AudioData>();
    audio->path = "loop.wav";
    audio->sampleRate = 48000;
    audio->channels = {drumrender::electroDrums(hits, Seconds)};
    audio->frames = std::int64_t(audio->channels[0].size());
    audio->buildWaveform();
    return audio;
}

// The project beats of the pattern clip's notes of `key`, from the song's start.
std::vector<double> noteBeats(const TrackListWrapper& tracks, int trackId, int clipId, int key)
{
    std::vector<double> beats;
    const auto* track = tracks.project().findTrack(trackId);
    for (const auto& c : track->audioClips)
        if (c.id == clipId) {
            const double origin = tracks.project().tempo().beatAt(c.start - c.sourceOffset);
            for (const auto& n : c.notes)
                if (n.note == key) beats.push_back(origin + n.startBeat);
        }
    return beats;
}

bool near(const std::vector<double>& beats, double beat, double within)
{
    for (double b : beats)
        if (std::abs(b - beat) < within) return true;
    return false;
}

} // namespace

TEST(qtdrumanalyzer, the_selected_clip_gives_tempo_meter_and_a_pattern_clip_in_time_with_it)
{
    Storage storage;
    TrackListWrapper tracks;                                     // 120 BPM, 48 kHz
    const int audioTrack = tracks.addAudioTrack();
    std::vector<drumrender::Timed> played;
    constexpr std::int64_t ClipStart = 2 * 48000;                // the clip starts at 2 s
    CHECK(tracks.attachTake(audioTrack, ClipStart, drumLoop(&played), false));
    const int clipId = tracks.project().findTrack(audioTrack)->audioClips.at(0).id;

    DrumAnalyzerWrapper w;
    CHECK(w.unavailableReason() == "No project is open.");
    w.setTrackModel(&tracks);
    CHECK(w.unavailableReason().contains("Select an audio clip"));
    CHECK(!w.analyse() && w.message().contains("Select an audio clip"));
    w.setClipId(clipId);
    CHECK(w.unavailableReason().isEmpty());
    CHECK(!w.clipLabel().isEmpty());

    int finishedOk = 0;
    QObject::connect(&w, &DrumAnalyzerWrapper::finished, [&](bool ok) { finishedOk += ok ? 1 : 0; });
    CHECK(w.analyse());
    CHECK(w.busy());
    CHECK(!w.analyse());                                         // one at a time
    CHECK(settle(w));
    CHECK(finishedOk == 1 && w.hasResult() && w.message().isEmpty());
    CHECK(std::abs(w.progress() - 1.0) < 1e-9);
    CHECK(std::abs(w.bpm() - LoopBpm) < 0.1);
    CHECK(w.numerator() == 4 && w.denominator() == 4);
    CHECK(w.kicks() > 0 && w.snares() > 0 && w.hats() > 0 && w.bars() >= 5);
    CHECK(w.previewColumns() == 2 * 4 * w.columnsPerBeat());
    const auto preview = w.preview();
    CHECK(!preview.isEmpty());
    for (const auto& cell : preview) {
        const auto m = cell.toMap();
        CHECK(m["row"].toInt() >= 0 && m["row"].toInt() <= 2);
        CHECK(m["column"].toInt() >= 0 && m["column"].toInt() < w.previewColumns());
    }

    // The tempo doubled and halved again, without a new analysis.
    CHECK(w.retempo(2.0) && std::abs(w.bpm() - 2 * LoopBpm) < 0.3);
    CHECK(w.retempo(0.5) && std::abs(w.bpm() - LoopBpm) < 0.2);

    // Targets: two new kinds first, then drum tracks only.
    auto targets = w.targets();
    CHECK(targets.size() == 2);
    CHECK(targets[0].toMap()["id"].toInt() == -1 && targets[1].toMap()["id"].toInt() == -2);
    CHECK(w.makeClip(audioTrack) == -1 && w.message().contains("drum track"));

    // The project left at 120 BPM: each hit lands at its real time.
    const int drumsCount = tracks.count();
    const int clip = w.makeClip(-1);
    CHECK(clip > 0);
    CHECK(tracks.count() == drumsCount + 1);
    const int drums = tracks.trackIdAt(tracks.count() - 1);
    CHECK(tracks.project().findTrack(drums)->type == "Drums");
    const auto kicks = noteBeats(tracks, drums, clip, 60);
    const double secondsToBeats = 120.0 / 60.0;
    for (const auto& h : played)
        if (h.key == 60) CHECK(near(kicks, (2.0 + h.seconds) * secondsToBeats, 0.03 * secondsToBeats));

    // At the found tempo the clip's notes keep their beats; an existing drum track takes it too.
    tracks.setTempo(w.bpm(), w.numerator(), w.denominator());
    targets = w.targets();
    CHECK(targets.size() == 3 && targets[2].toMap()["id"].toInt() == drums);
    const int second = w.makeClip(drums);
    CHECK(second > 0 && second != clip);
    const auto snares = noteBeats(tracks, drums, second, 62);
    const double perSecond = LoopBpm / 60.0;
    int found = 0, expected = 0;
    for (const auto& h : played)
        if (h.key == 62) { ++expected; found += near(snares, (2.0 + h.seconds) * perSecond, 0.03 * perSecond) ? 1 : 0; }
    CHECK(expected > 0 && found == expected);
    // Bar 1 lies on a found bar line: the clip's first beat is the loop's first downbeat.
    const auto* made = tracks.project().findTrack(drums)->audioClips.data() + 1;
    CHECK(std::abs(tracks.project().tempo().beatAt(made->start) - (2.0 + LoopStart) * perSecond) < 0.03 * perSecond);
}

TEST(qtdrumanalyzer, the_typical_bars_become_a_groove_and_a_taken_name_is_refused)
{
    Storage storage;
    TrackListWrapper tracks;
    const int audioTrack = tracks.addAudioTrack();
    CHECK(tracks.attachTake(audioTrack, 0, drumLoop(), false));
    DrumAnalyzerWrapper w;
    const QString folder = storage.dir.path() + "/grooves";
    w.setGrooveFolder(folder);
    CHECK(w.grooveFolder() == folder);
    w.setTrackModel(&tracks);
    w.setClipId(tracks.project().findTrack(audioTrack)->audioClips.at(0).id);
    CHECK(!w.saveGroove("Too soon") && w.message().contains("Analyse"));
    CHECK(w.analyse() && settle(w) && w.hasResult());

    CHECK(w.suggestedGrooveName().endsWith(" groove"));
    CHECK(w.suggestedGrooveName().size() <= 48);
    CHECK(!w.saveGroove("  ") && w.message().contains("name"));
    CHECK(w.saveGroove("Found beat"));
    CHECK(QDir(folder).entryList({"*.groove"}, QDir::Files).size() == 1);
    GrooveStore store(folder.toStdString());
    const auto groove = store.find("Found beat");
    CHECK(groove.has_value());
    if (groove) {
        CHECK(groove->numerator == 4 && groove->denominator == 4 && groove->bars == 2 && !groove->factory);
        int kicks = 0, snares = 0, hats = 0;
        for (const auto& n : groove->notes) {
            CHECK(n.startBeat >= 0.0 && n.startBeat < 8.0);
            kicks += n.note == 60; snares += n.note == 62; hats += n.note == 66 || n.note == 70;
        }
        CHECK(kicks == 6 && snares == 4 && hats == 16);        // two bars of the rock beat
    }
    CHECK(!w.saveGroove("found BEAT") && w.message().contains("already"));
}

TEST(qtdrumanalyzer, midi_short_and_cancelled_clips_say_why)
{
    Storage storage;
    TrackListWrapper tracks;
    const int audioTrack = tracks.addAudioTrack();
    auto shortAudio = std::make_shared<AudioData>();
    shortAudio->path = "short.wav"; shortAudio->sampleRate = 48000; shortAudio->frames = 2 * 48000;
    shortAudio->channels = {std::vector<float>(std::size_t(shortAudio->frames), 0.1f)};
    shortAudio->buildWaveform();
    CHECK(tracks.attachTake(audioTrack, 0, shortAudio, false));
    DrumAnalyzerWrapper w;
    w.setTrackModel(&tracks);
    w.setClipId(tracks.project().findTrack(audioTrack)->audioClips.at(0).id);
    CHECK(w.unavailableReason().contains("too short"));

    const int drums = tracks.addInstrumentTrackOfType("Drums");
    const int pattern = tracks.addClipAtBeat(drums, 0, 4);
    w.setClipId(pattern);
    CHECK(w.unavailableReason().contains("MIDI clip"));

    // Cancelled at once: no result, and it says so.
    const int loopTrack = tracks.addAudioTrack();
    CHECK(tracks.attachTake(loopTrack, 0, drumLoop(), false));
    w.setClipId(tracks.project().findTrack(loopTrack)->audioClips.at(0).id);
    CHECK(w.analyse());
    w.cancel();
    CHECK(settle(w));
    CHECK(!w.hasResult() && w.message() == "Cancelled.");
    CHECK(w.makeClip(-1) == -1 && w.message().contains("Analyse"));
}
