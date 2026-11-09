#include "testkit.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/plugins/chordplayer/chordswrap/chordswrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "wrapper/settings/appsettingswrap/appsettingswrapper.h"
#include "wrapper/pianorollwrap/pianorollwrapper.h"
#include "audiodevice/audiodevice.h"
#include "instruments/chords/chordsinstrument.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>

#include <set>

namespace {

// Renders one 128-frame block at a time; a block may carry MIDI from a "keyboard".
class OfflineDevice final : public AudioDevice {
public:
    AudioCallback* callback = nullptr;
    bool open(AudioCallback* c, int frames) override { callback = c; c->deviceChanged(48000, frames); return true; }
    void close() override { callback = nullptr; }
    double sampleRate() const override { return 48000; }
    DeviceLatency latency() const override { return {128, 128, 128}; }
    bool setBufferSize(int) override { return true; }
    std::vector<std::string> inputNames() const override { return {"test:input"}; }
    std::vector<std::string> outputNames() const override { return {"test:left", "test:right"}; }
    bool connectInput(const std::string&) override { return true; }
    int connectInputChannel(const std::string&) override { return 0; }
    bool connectOutput(const std::string&, const std::string&) override { return true; }
    std::vector<std::string> midiInputNames() const override { return {"keyboard:midi_out"}; }
    std::string lastError() const override { return {}; }
    float input[128]{}, left[128]{}, right[128]{};
    void process(MidiBuffer* midi = nullptr, int blocks = 1)
    {
        for (int b = 0; b < blocks; ++b) {
            float* in[]{input}; float* out[]{left, right};
            if (callback) callback->process({{in, 1, 128}, {out, 2, 128}, b == 0 ? midi : nullptr, 128});
        }
    }
};

void tick(int ms = 30) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }

// Throwaway settings and data folders: the tests never touch the real app's files.
struct Storage {
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-chords-XXXXXX")};
    Storage() {
        if (!QCoreApplication::instance()) {
            static int argc = 1; static char name[] = "chordstests"; static char* argv[]{name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerChordsTests");
        QCoreApplication::setApplicationName("Tests");
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        qputenv("CHAOSMAKER_SAMPLE_KITS", (dir.path() + "/no-sample-kits").toUtf8());   // never the real piano
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    }
};

const ChordPlayer* playerOf(EngineController& controller, int track)
{
    const auto instrument = controller.trackInstrument(track);
    if (!instrument || instrument->typeId() != "chords") return nullptr;
    return &static_cast<const ChordsInstrument&>(*instrument).player();
}

} // namespace

TEST(qtchords, the_page_edits_the_track_and_its_player_follows)
{
    Storage storage;
    EngineController controller(std::make_unique<OfflineDevice>(), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int id = tracks.addTrackChoice("Chords");
    const int piano = tracks.addInstrumentTrackOfType("Piano");
    CHECK(id >= 0 && playerOf(controller, id));

    ChordsWrapper page;
    page.setTrackModel(&tracks); page.setTrackId(id); page.setEngine(&controller);
    CHECK(page.valid());
    CHECK((page.chords() == QStringList{"C", "Dm", "Em", "F", "G", "Am", "Bdim", "G7"}));
    CHECK(page.autoplay() == 3 && page.keyName() == "C major");
    CHECK(page.autoplayModes().size() == 5 && page.progressions().size() >= 6);
    // Each strip's zones, from its chord's root (C Dm Em F G Am Bdim G7): C3–C5, C4–C6, C5–C7 ...
    const auto u = [](const char* text) { return QString::fromUtf8(text); };
    CHECK((page.zoneRanges()[0].toStringList() == QStringList{u("C3\u2013C5"), u("C4\u2013C6"), u("C5\u2013C7")}));
    CHECK((page.zoneRanges()[4].toStringList() == QStringList{u("G3\u2013G5"), u("G4\u2013G6"), u("G5\u2013G7")}));
    CHECK(page.degreeLabels().size() == 7 && page.degreeLabels()[4] == "V");

    int changes = 0;
    QObject::connect(&tracks, &TrackListWrapper::chordsChanged, &tracks, [&changes] { ++changes; });
    CHECK(page.setChord(0, "Amin7") && page.chords()[0] == "Am7");
    CHECK(!page.setChord(1, "H7") && page.lastError().contains("H7") && page.chords()[1] == "Dm");
    CHECK(page.setChord(1, "") && page.chords()[1].isEmpty());
    CHECK(page.zoneRanges()[1].toStringList().isEmpty());              // an empty strip: no ranges
    CHECK(page.zoneRanges()[0].toStringList().first() == QString::fromUtf8("A3\u2013A5"));   // Am7 now
    CHECK(page.degreeChord(5, true) == "G7" && page.setDegree(2, 5, true) && page.chords()[2] == "G7");
    // Chords of your own: no progression; a new key or mode fills the key chords.
    CHECK(page.currentProgression() == -1);
    page.setKey(0, true);
    CHECK(page.keyName() == "C minor" && page.degreeLabels()[0] == "i" && page.currentProgression() == 0);
    CHECK((page.chords() == QStringList{"Cm", "Ddim", "Eb", "Fm", "Gm", "Ab", "Bb", "G7"}));
    page.setKey(0, false);
    CHECK((page.chords() == QStringList{"C", "Dm", "Em", "F", "G", "Am", "Bdim", "G7"}));
    // The list is named for the mode; a progression stays when the key or mode changes.
    int pop = -1, andalusian = -1;
    for (const auto& p : page.progressions()) {
        const auto name = p.toMap().value("name").toString();
        if (name.startsWith("Pop")) pop = p.toMap().value("index").toInt();
        if (name.startsWith("Andalusian")) andalusian = p.toMap().value("index").toInt();
    }
    CHECK(page.progressions()[pop].toMap().value("name").toString() == QString::fromUtf8("Pop I\u2013V\u2013vi\u2013IV"));
    CHECK(pop >= 0 && page.applyProgression(pop) && page.currentProgression() == pop);
    CHECK((page.chords().mid(0, 4) == QStringList{"C", "G", "Am", "F"}));
    page.setKey(0, true);
    CHECK(page.currentProgression() == pop && (page.chords().mid(0, 4) == QStringList{"Cm", "Gm", "Ab", "Fm"}));
    CHECK(page.progressions()[pop].toMap().value("name").toString() == QString::fromUtf8("Pop i\u2013v\u2013VI\u2013iv"));
    page.setKey(9, true);
    CHECK(page.keyName() == "A minor" && page.tonicNames()[9] == "A" && (page.chords().mid(0, 4) == QStringList{"Am", "Em", "F", "Dm"}));
    CHECK(andalusian >= 0 && page.applyProgression(andalusian));
    CHECK((page.chords().mid(0, 4) == QStringList{"Am", "G", "F", "E"}));
    page.setAutoplay(1);
    CHECK(tracks.chords(id)->autoplay == 1 && changes >= 5);
    CHECK(controller.projectDirty());

    // A second page on the same track follows; a page on another track type does nothing.
    ChordsWrapper other;
    other.setTrackModel(&tracks); other.setTrackId(id);
    CHECK(other.chords() == page.chords() && other.autoplay() == 1);
    ChordsWrapper notChords;
    notChords.setTrackModel(&tracks); notChords.setTrackId(piano);
    CHECK(!notChords.valid() && !notChords.setChord(0, "C"));
    CHECK(page.resetToDefaults() && page.chords()[0] == "C" && other.chords()[0] == "C");
    controller.shutdownSession();
}

TEST(qtchords, strips_and_midi_keys_play_and_autoplay_keeps_going_while_stopped)
{
    Storage storage;
    auto device = std::make_unique<OfflineDevice>();
    auto* io = device.get();
    EngineController controller(std::move(device), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int id = tracks.addTrackChoice("Chords");
    ChordsWrapper page;
    page.setTrackModel(&tracks); page.setTrackId(id); page.setEngine(&controller);
    page.setWatchActive(true);
    tick(); io->process();
    const auto* player = playerOf(controller, id);
    CHECK(player);
    if (!player) return;

    // A strip pressed and let go with the transport stopped: it latches and keeps playing.
    page.press(5, false, 100);                                       // Am
    io->process(nullptr, 2);
    page.release(5, false);
    io->process(nullptr, 40);                                        // about 0.1 s, nothing held
    CHECK(player->activeStrip() == 5 && player->running());
    tick(80);
    CHECK(page.activeStrip() == 5 && !page.activeBass());
    page.press(5, false, 100);                                       // the same strip again: it stops
    io->process(nullptr, 2);
    page.release(5, false);
    io->process(nullptr, 2);
    CHECK(!player->running() && player->activeStrip() == -1);

    // A MIDI keyboard plays the strips: D4 is strip two's chord, D2 its bass.
    MidiBuffer keys;
    keys.add(MidiEvent{0, 0x90, 62, 100});
    io->process(&keys);
    io->process(nullptr, 4);
    CHECK(player->activeStrip() == 1 && !player->activeBass());
    keys.clear();
    keys.add(MidiEvent{0, 0x90, 38, 100});
    io->process(&keys);
    CHECK(player->activeStrip() == 1 && player->activeBass());
    tick(80);
    CHECK(page.activeStrip() == 1 && page.activeBass());
    controller.shutdownSession();
}

TEST(qtchords, a_time_signature_change_reaches_the_patterns)
{
    Storage storage;
    auto device = std::make_unique<OfflineDevice>();
    auto* io = device.get();
    EngineController controller(std::move(device), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int id = tracks.addTrackChoice("Chords");
    ChordsWrapper page;
    page.setTrackModel(&tracks); page.setTrackId(id); page.setEngine(&controller);
    tick(); io->process();
    const auto* player = playerOf(controller, id);
    CHECK(player);
    if (!player) return;
    page.press(0, false, 100);
    io->process(nullptr, 2);
    CHECK(player->patternNumerator() == 4 && player->patternDenominator() == 4);
    // Changed with nothing else edited: the next block plays the new meter's pattern.
    for (const auto [n, d] : {std::pair{3, 4}, std::pair{6, 8}, std::pair{7, 8}}) {
        controller.setMeter(n, d);
        io->process(nullptr, 2);
        CHECK(player->patternNumerator() == n && player->patternDenominator() == d);
    }
    page.release(0, false);
    controller.shutdownSession();
}

TEST(qtchords, recording_keeps_the_presses_which_play_back_and_convert_to_piano_notes)
{
    Storage storage;
    auto device = std::make_unique<OfflineDevice>();
    auto* io = device.get();
    EngineController controller(std::move(device), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    controller.setBpm(120);
    controller.setMeter(4, 4);
    const int id = tracks.addTrackChoice("Chords");
    ChordsWrapper page;
    page.setTrackModel(&tracks); page.setTrackId(id); page.setEngine(&controller);
    CHECK(tracks.setRecordArmed(id, true));
    tick(); io->process();

    controller.transportAction("record");
    CHECK(controller.takeBusy());
    io->process();
    page.press(5, false, 100);                                       // Am
    io->process();
    page.release(5, false);
    io->process(nullptr, 375);                                       // one bar at 120 BPM (2 s)
    page.press(4, false, 100);                                       // G is strip five (C Dm Em F G ...)
    io->process();
    page.release(4, false);
    io->process(nullptr, 375);
    controller.transportAction("stop");
    io->process();
    for (int i = 0; i < 100 && controller.takeBusy(); ++i) tick(10);
    CHECK(!controller.takeBusy());

    // The take holds the presses on the trigger keys, not the notes they played.
    const auto clips = tracks.clips();
    CHECK(clips.size() == 1);
    if (clips.size() != 1) return;
    const auto clip = clips[0].toMap();
    const int clipId = clip["clipId"].toInt();
    std::set<int> keys;
    for (const auto& n : clip["notes"].toList()) keys.insert(n.toMap()["note"].toInt());
    CHECK((keys == std::set<int>{69, 67}));                          // strips six (A4) and five (G4)

    // Played back, the player plays them again.
    const auto* player = playerOf(controller, id);
    CHECK(player);
    controller.seekBeat(0);
    controller.transportAction("play");
    io->process(nullptr, 40);
    CHECK(player && player->activeStrip() == 5);
    io->process(nullptr, 400);
    CHECK(player && player->activeStrip() == 4);
    controller.transportAction("stop");
    io->process(nullptr, 4);

    // Convert: a new Piano track with the notes the strips played, at the same place.
    const int countBefore = tracks.count();
    const int pianoTrack = page.convertClipToPianoTrack(clipId);
    CHECK(pianoTrack >= 0 && tracks.count() == countBefore + 1);
    CHECK(tracks.project().findTrack(pianoTrack)->type == "Piano");
    std::vector<int> notes;
    std::set<int> classes;
    for (const auto& c : tracks.domain().clips())
        if (c.trackId == pianoTrack) for (const auto& n : c.notes) { notes.push_back(n.note); classes.insert(n.note % 12); }
    CHECK(notes.size() > 8);                                         // an arpeggio, not two presses
    CHECK(classes.count(9) && classes.count(0) && classes.count(4)); // A C E
    CHECK(classes.count(7) && classes.count(11) && classes.count(2)); // G B D
    CHECK(page.convertClipToPianoTrack(9999) == -1 && !page.lastError().isEmpty());
    controller.shutdownSession();
}

TEST(qtchords, zones_follow_the_preferred_mapping_and_the_name_plays_the_whole_strip)
{
    Storage storage;
    auto device = std::make_unique<OfflineDevice>();
    auto* io = device.get();
    EngineController controller(std::move(device), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int id = tracks.addTrackChoice("Chords");
    ChordsWrapper page;
    page.setTrackModel(&tracks); page.setTrackId(id); page.setEngine(&controller);
    page.setWatchActive(true);
    tick(); io->process();
    const auto* player = playerOf(controller, id);
    CHECK(player);
    if (!player) return;
    const auto play = [&](auto&& action) { action(); io->process(nullptr, 4); };

    // The default mapping: each zone its own octave.
    AppSettingsWrapper settings;
    CHECK(settings.chordZoneMapping() == "octaves" && AppSettingsWrapper::storedChordZoneMapping() == "octaves");
    play([&] { page.press(2, false, 100, 2); page.release(2, false); });
    CHECK(player->activeStrip() == 2 && player->activeZone() == ChordPlayer::High);
    tick(80);
    CHECK(page.activeStrip() == 2 && page.activeZone() == 2 && !page.activeWhole());

    // Zone keys: the same zones, written as a zone key before the middle-octave press.
    settings.setChordZoneMapping("zonekeys");
    CHECK(AppSettingsWrapper::storedChordZoneMapping() == "zonekeys");
    play([&] { page.press(3, false, 100, 0); page.release(3, false); });
    CHECK(player->activeStrip() == 3 && player->activeZone() == ChordPlayer::Low);
    play([&] { page.press(3, false, 100, 1); page.release(3, false); });   // another zone re-voices, it does not stop
    CHECK(player->activeStrip() == 3 && player->activeZone() == ChordPlayer::Middle && player->running());
    settings.setChordZoneMapping("octaves");

    // The name plays the whole strip (middle zone) and lights both areas until another press.
    play([&] { page.pressWhole(5, 100); page.releaseWhole(5); });
    tick(80);
    CHECK(player->activeStrip() == 5 && !player->activeBass() && player->activeZone() == ChordPlayer::Middle);
    CHECK(page.activeStrip() == 5 && page.activeWhole());
    play([&] { page.press(5, true, 100); page.release(5, true); });       // its bass area alone
    tick(80);
    CHECK(page.activeStrip() == 5 && page.activeBass() && !page.activeWhole());
    controller.shutdownSession();
}

TEST(qtchords, the_piano_roll_names_every_zone_key)
{
    Storage storage;
    TrackListWrapper tracks;
    const int id = tracks.addTrackChoice("Chords");                 // C Dm Em F G Am Bdim G7
    PianoRollWrapper roll;
    roll.setTrackList(&tracks);
    roll.setTrackId(id);
    CHECK(roll.drumName(65) == QString::fromUtf8("4 \u00b7 F"));
    CHECK(roll.drumName(17) == QString::fromUtf8("4 low \u00b7 F"));
    CHECK(roll.drumName(89) == QString::fromUtf8("4 high \u00b7 F"));
    CHECK(roll.drumName(41) == QString::fromUtf8("Bass 4 \u00b7 F"));
    CHECK(roll.drumName(42) == "Zone low" && roll.drumName(44) == "Zone middle" && roll.drumName(46) == "Zone high");
    CHECK(roll.drumName(61).isEmpty());
}

TEST(qtchords, volume_and_bass_with_chords_are_saved_on_the_track_and_reach_the_player)
{
    Storage storage;
    auto device = std::make_unique<OfflineDevice>();
    auto* io = device.get();
    EngineController controller(std::move(device), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int id = tracks.addTrackChoice("Chords");
    ChordsWrapper page;
    page.setTrackModel(&tracks); page.setTrackId(id); page.setEngine(&controller);
    CHECK(page.dynamics() == 0 && page.bassWithChords());              // the defaults
    tick(); io->process();
    const auto* player = playerOf(controller, id);
    CHECK(player && player->dynamics() == chords::DynamicsEven && player->bassWithChords());
    if (!player) return;
    // Each edit reaches the playing track's player at once (the bug of the first version: only
    // the strips and the autoplay did, until the project was loaded again).
    page.setDynamics(1);
    CHECK(tracks.chords(id)->dynamics == chords::DynamicsTopNote && page.dynamics() == 1);
    CHECK(player->dynamics() == chords::DynamicsTopNote);
    page.setBassWithChords(false);
    CHECK(!tracks.chords(id)->bassWithChords && !page.bassWithChords());
    CHECK(!player->bassWithChords());
    page.setDynamics(2);
    CHECK(tracks.chords(id)->dynamics == chords::DynamicsMeter && player->dynamics() == chords::DynamicsMeter);
    page.setDynamics(1);
    page.setDynamics(5);                                               // out of range: ignored
    CHECK(page.dynamics() == 1 && player->dynamics() == chords::DynamicsTopNote);
    page.setDynamics(0);
    page.setBassWithChords(true);
    CHECK(player->dynamics() == chords::DynamicsEven && player->bassWithChords());
    controller.shutdownSession();
}
