#include "testkit.h"
#include "analysis/chordrender.h"
#include "plugins/chordplayer/chordplayer.h"
#include "wrapper/analysis/chordanalyzerwrap/chordanalyzerwrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>

// The chord analyzer's bridge (restMap G21): the selected clip in, a Chords track's strips and
// a clip that plays them out.
namespace {

struct Storage {
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-chordanalyzer-XXXXXX")};
    Storage() {
        if (!QCoreApplication::instance()) {
            static int argc = 1; static char name[] = "chordanalyzertests"; static char* argv[]{name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerChordAnalyzerTests");
        QCoreApplication::setApplicationName("Tests");
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    }
};

void tick(int ms = 20) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }

bool settle(ChordAnalyzerWrapper& w)
{
    for (int i = 0; i < 1500 && w.busy(); ++i) tick(20);
    return !w.busy();
}

constexpr double SongBpm = 100.0;

// C | Am | F | G7, four times, at 100 BPM, as a recorded clip's audio.
std::shared_ptr<AudioData> song()
{
    using namespace chordrender;
    auto audio = std::make_shared<AudioData>();
    audio->path = "song.wav";
    audio->sampleRate = rate;
    audio->channels = {chordrender::song(repeated({{0, major}, {9, minor}, {5, major}, {7, seventh}}, 4), SongBpm)};
    audio->frames = std::int64_t(audio->channels[0].size());
    audio->buildWaveform();
    return audio;
}

} // namespace

TEST(qtchordanalyzer, the_selected_song_gives_its_key_and_chords_and_fills_a_chords_track)
{
    Storage storage;
    TrackListWrapper tracks;                                     // 120 BPM
    const int audioTrack = tracks.addAudioTrack();
    const double rate = tracks.project().tempo().sampleRate();
    const auto clipStart = std::int64_t(2 * rate);               // the clip starts at 2 s
    CHECK(tracks.attachTake(audioTrack, clipStart, song(), false));
    const int clipId = tracks.project().findTrack(audioTrack)->audioClips.at(0).id;

    ChordAnalyzerWrapper w;
    CHECK(w.unavailableReason() == "No project is open.");
    w.setTrackModel(&tracks);
    CHECK(w.unavailableReason().contains("Select the song"));
    CHECK(!w.analyse() && w.message().contains("Select the song"));
    CHECK(w.putOnTrack(-1, false) == -1 && w.message().contains("Analyse"));
    w.setClipId(clipId);
    CHECK(w.unavailableReason().isEmpty() && !w.clipLabel().isEmpty());
    CHECK(!w.hasResult() && w.chords().isEmpty() && w.chartText().isEmpty());

    int finishedOk = 0;
    QObject::connect(&w, &ChordAnalyzerWrapper::finished, [&](bool ok) { finishedOk += ok ? 1 : 0; });
    CHECK(w.analyse());
    CHECK(w.busy() && !w.analyse());                             // one at a time
    CHECK(settle(w));
    CHECK(finishedOk == 1 && w.hasResult() && w.message().isEmpty());
    CHECK(std::abs(w.progress() - 1.0) < 1e-9);
    CHECK(w.keyName() == "C major");
    CHECK(w.hasBeat() && std::abs(w.bpm() - SongBpm) < 0.5 && w.numerator() == 4 && w.denominator() == 4);
    CHECK(std::abs(w.tuningCents()) < 10.0 && w.seconds() > 30.0);
    // Triads to start with; sevenths when asked, without a new analysis.
    CHECK(!w.sevenths());
    CHECK(w.commonChords().size() == 4 && w.commonChords().contains("G") && !w.commonChords().contains("G7"));
    int changes = 0;
    QObject::connect(&w, &ChordAnalyzerWrapper::resultChanged, [&] { ++changes; });
    w.setSevenths(true);
    CHECK(changes == 1 && w.commonChords().contains("G7") && !w.commonChords().contains("G"));
    const auto list = w.chords();
    CHECK(list.size() >= 16);
    double at = 0.0;
    for (const auto& item : list) {
        const auto m = item.toMap();
        CHECK(std::abs(m["from"].toDouble() - at) < 1e-9 && m["to"].toDouble() > at && m["beats"].toInt() >= 1);
        CHECK(m["sure"].toDouble() >= 0.0 && m["sure"].toDouble() <= 1.0);
        at = m["to"].toDouble();
    }
    const auto chart = w.chartText();
    CHECK(chart.startsWith("Key: C major") && chart.contains("100 BPM") && chart.contains("0:00") && chart.contains("Am") && chart.contains("G7"));
    w.copyChart();                                               // no screen here: does nothing, and must not crash

    // Targets: a new Chords track, then the project's Chords tracks only.
    CHECK(w.targets().size() == 1 && w.targets()[0].toMap()["id"].toInt() == -1);
    CHECK(w.putOnTrack(audioTrack, false) == -1 && w.message().contains("Chords track"));

    // The strips and the key, no clip.
    const int before = tracks.count();
    const int chordsTrack = w.putOnTrack(-1, false);
    CHECK(chordsTrack >= 0 && tracks.count() == before + 1);
    CHECK(tracks.project().findTrack(chordsTrack)->type == "Chords");
    const auto* settings = tracks.chords(chordsTrack);
    CHECK(settings != nullptr);
    if (!settings) return;
    const auto common = w.commonChords();
    for (int i = 0; i < 8; ++i) CHECK(QString::fromStdString(settings->chords[std::size_t(i)]) == (i < common.size() ? common[i] : QString()));
    CHECK(settings->key == (chords::Key{0, false}));
    CHECK(tracks.project().findTrack(chordsTrack)->audioClips.empty());
    CHECK(w.message().contains("strips"));
    CHECK(w.targets().size() == 2 && w.targets()[1].toMap()["id"].toInt() == chordsTrack);

    // The same track again, with the clip: each chord's strip held from its real time (the
    // project is at 120 BPM, the song at 100: 2 project beats a second).
    CHECK(w.putOnTrack(chordsTrack, true) == chordsTrack && tracks.count() == before + 1);
    const auto& clips = tracks.project().findTrack(chordsTrack)->audioClips;
    CHECK(clips.size() == 1);
    if (clips.size() != 1) return;
    const auto& tempo = tracks.project().tempo();
    const double origin = tempo.beatAt(clips[0].start - clips[0].sourceOffset);
    CHECK(clips[0].notes.size() >= 16);
    const auto& spans = w.result()->chords;
    std::size_t matched = 0;
    for (const auto& span : spans) {
        if (span.root < 0) continue;
        const int strip = int(common.indexOf(QString::fromStdString(span.name)));
        const double beat = tempo.beatAt(clipStart + std::int64_t(std::llround(span.fromSeconds * rate)));
        for (const auto& n : clips[0].notes)
            if (n.note == ChordPlayer::ChordKeys[std::size_t(strip)] && std::abs(origin + n.startBeat - beat) < 1e-3) {
                CHECK(std::abs(n.lengthBeats - (span.toSeconds - span.fromSeconds) * 2.0) < 1e-2);
                ++matched;
                break;
            }
    }
    CHECK(matched == clips[0].notes.size() && matched >= 16);
    CHECK(w.message().contains("clip"));

    // The song lined up with the project's bars once the project is at the found tempo: the
    // audio clip slides by less than a bar, so the bar line found in it sits on a project bar.
    CHECK(w.hasBeat());
    tracks.setTempo(w.bpm(), w.numerator(), w.denominator());
    const double barSamples = tempo.beatsPerBar() * 60.0 * rate / tempo.bpm();
    const auto audioStart = [&] { return tracks.project().findTrack(audioTrack)->audioClips.at(0).start; };
    const std::int64_t startBefore = audioStart();
    CHECK(w.alignSong());
    const std::int64_t shift = audioStart() - startBefore;
    CHECK(std::abs(double(shift)) < barSamples);
    const double barLine = double(audioStart()) + w.result()->firstBarSeconds * rate;
    CHECK(std::abs(std::remainder(barLine, barSamples)) < 2.0);
    CHECK(w.putOnTrack(chordsTrack, false) == chordsTrack);
    CHECK(w.message().contains("The song moved") == (shift != 0));
    CHECK(w.alignSong() && audioStart() == startBefore + shift);                // lined up already
    if (shift != 0) { CHECK(tracks.undo()); CHECK(audioStart() == startBefore); }
}

TEST(qtchordanalyzer, midi_short_and_cancelled_clips_say_why)
{
    Storage storage;
    TrackListWrapper tracks;
    ChordAnalyzerWrapper w;
    w.setTrackModel(&tracks);

    // A MIDI clip.
    const int chordsTrack = tracks.addInstrumentTrackOfType("Chords");
    const int pattern = tracks.addPatternClipAtBeat(chordsTrack, 0.0, 8.0, {});
    w.setClipId(pattern);
    CHECK(w.unavailableReason().contains("MIDI clip"));

    // Too short.
    const int audioTrack = tracks.addAudioTrack();
    auto shortAudio = std::make_shared<AudioData>();
    shortAudio->sampleRate = 48000;
    shortAudio->channels = {std::vector<float>(48000, 0.1f)};
    shortAudio->frames = 48000;
    shortAudio->buildWaveform();
    CHECK(tracks.attachTake(audioTrack, 0, shortAudio, false));
    w.setClipId(tracks.project().findTrack(audioTrack)->audioClips.at(0).id);
    CHECK(w.unavailableReason().contains("too short"));

    // Cancelled: no result, and it says so.
    const int songTrack = tracks.addAudioTrack();
    CHECK(tracks.attachTake(songTrack, 0, song(), false));
    w.setClipId(tracks.project().findTrack(songTrack)->audioClips.at(0).id);
    int finishedOk = 0, finished = 0;
    QObject::connect(&w, &ChordAnalyzerWrapper::finished, [&](bool ok) { ++finished; finishedOk += ok ? 1 : 0; });
    CHECK(w.analyse());
    w.cancel();
    CHECK(settle(w));
    CHECK(finished == 1 && finishedOk == 0 && !w.hasResult() && w.message() == "Cancelled.");
    CHECK(w.putOnTrack(-1, true) == -1 && w.message().contains("Analyse"));
}
