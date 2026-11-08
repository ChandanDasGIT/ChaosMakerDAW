#include "testkit.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/instruments/electrodrums/electrodrumswrap/electrodrumswrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "audiodevice/audiodevice.h"
#include "instruments/electrodrums/electrodrums.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QSettings>
#include <QTemporaryDir>

namespace {

class SilentDevice final : public AudioDevice {
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
    std::string lastError() const override { return {}; }
};

// Plays one block at a time and can deliver MIDI from a "keyboard".
class KeyboardDevice final : public AudioDevice {
public:
    AudioCallback* callback = nullptr;
    MidiBuffer midi;
    float input[128]{}, left[128]{}, right[128]{};
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
    std::vector<std::string> midiInputNames() const override { return {"piano:midi_out"}; }
    std::string lastError() const override { return {}; }
    // Renders `blocks` blocks; the MIDI goes into the first. Returns the loudest output sample.
    float play(int blocks)
    {
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b) {
            float* in[]{input}; float* out[]{left, right};
            std::fill(std::begin(left), std::end(left), 0.0f);
            std::fill(std::begin(right), std::end(right), 0.0f);
            if (callback) callback->process({{in, 1, 128}, {out, 2, 128}, b == 0 ? &midi : nullptr, 128});
            if (b == 0) midi.clear();
            for (const float v : left) peak = std::max(peak, std::abs(v));
        }
        return peak;
    }
};

void tick(int ms = 30) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }

// Throwaway settings and data folders: the tests never touch the real app's files.
struct Storage {
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-electrodrums-XXXXXX")};
    Storage() {
        if (!QCoreApplication::instance()) {
            static int argc = 1; static char name[] = "electrodrumstests"; static char* argv[]{name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerElectroDrumsTests");
        QCoreApplication::setApplicationName("Tests");
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    }
};

} // namespace

using electrodrums::Drum;

TEST(qtelectrodrums, wrapper_edits_the_track_and_follows_other_panels)
{
    Storage storage;
    TrackListWrapper tracks;
    const int id = tracks.addInstrumentTrackOfType("Electronic Drums");
    const int acoustic = tracks.addInstrumentTrackOfType("Drums");
    CHECK(tracks.electroDrums(id) != nullptr);
    CHECK(tracks.electroDrums(acoustic) == nullptr);

    ElectroDrumsWrapper a, b;
    a.setTrackModel(&tracks); a.setTrackId(id);
    b.setTrackModel(&tracks); b.setTrackId(id);
    CHECK(a.valid());
    CHECK(a.style() == "808" && a.volume() == 0.0 && a.decay("kick") == 1.0);
    int changes = 0;
    QObject::connect(&tracks, &TrackListWrapper::electroDrumsChanged, &tracks, [&changes] { ++changes; });

    a.setStyle("909");
    a.setStyle("606");                                           // unknown: ignored
    a.setVolume(-3.0);
    a.setTune("kick", -4.0);
    a.setDecay("clap", 2.0);
    a.setLevel("hihatOpen", -6.0);
    a.setLevel("cowbell", -6.0);                                 // unknown drum: ignored
    CHECK(changes == 5);
    const auto* stored = tracks.electroDrums(id);
    CHECK(stored->style == electrodrums::Style::Style909 && stored->volumeDb == -3.0);
    CHECK(stored->drum(Drum::Kick).tuneSemitones == -4.0 && stored->drum(Drum::Clap).decay == 2.0);
    CHECK(stored->drum(Drum::HatOpen).levelDb == -6.0);
    CHECK(b.style() == "909" && b.tune("kick") == -4.0 && b.decay("clap") == 2.0 && b.level("hihatOpen") == -6.0);
    CHECK(b.drumSettings().value("clap").toMap().value("decay").toDouble() == 2.0);
    CHECK(b.drumSettings().size() == electrodrums::DrumCount);

    a.setTune("snare", 40.0);                                    // clamped to what is stored
    CHECK(a.tune("snare") == electrodrums::MaxTune);
    a.setDecay("snare", 0.0);
    CHECK(a.decay("snare") == electrodrums::MinDecay);

    CHECK(a.resetToDefaults());
    CHECK(*tracks.electroDrums(id) == electrodrums::Settings{});
    CHECK(a.styles().size() == 3 && a.drums().size() == electrodrums::DrumCount);
    CHECK(a.drumTitle("hihatPedal") == "Hi-hat pedal" && a.drumTitle("cowbell").isEmpty());

    ElectroDrumsWrapper other;                                   // the acoustic kit's track
    other.setTrackModel(&tracks); other.setTrackId(acoustic);
    CHECK(!other.valid());
    other.setVolume(-3.0);
    CHECK(!other.resetToDefaults());
    CHECK(!tracks.setElectroDrums(acoustic, electrodrums::Settings{}));
}

TEST(qtelectrodrums, the_track_plays_the_kit_and_edits_reach_it)
{
    Storage storage;
    EngineController controller(std::make_unique<SilentDevice>(), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int id = tracks.addInstrumentTrackOfType("Electronic Drums");
    const auto instrument = controller.trackInstrument(id);
    CHECK(instrument && instrument->typeId() == "electrodrums");
    if (!instrument || instrument->typeId() != "electrodrums") return;
    const auto& kit = static_cast<const ElectroDrums&>(*instrument);

    ElectroDrumsWrapper page;
    page.setTrackModel(&tracks); page.setTrackId(id);
    page.setStyle("studio");
    page.setLevel("snare", -4.0);
    CHECK(kit.settings().style == electrodrums::Style::Studio);
    CHECK(kit.settings().drum(Drum::Snare).levelDb == -4.0);
    CHECK(controller.projectDirty());

    CHECK(controller.setBufferSize(256));                        // a new instrument keeps them
    const auto fresh = controller.trackInstrument(id);
    CHECK(fresh && fresh != instrument && fresh->typeId() == "electrodrums");
    if (fresh && fresh->typeId() == "electrodrums")
        CHECK(static_cast<const ElectroDrums&>(*fresh).settings() == *tracks.electroDrums(id));
    controller.shutdownSession();
}

TEST(qtelectrodrums, the_picture_plays_pads_and_sees_every_hit)
{
    Storage storage;
    auto device = std::make_unique<KeyboardDevice>();
    auto* keyboard = device.get();
    EngineController controller(std::move(device), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int id = tracks.addInstrumentTrackOfType("Electronic Drums");
    tick(); keyboard->play(2);

    ElectroDrumsWrapper page;
    CHECK(page.pads().size() == 16);
    CHECK(page.pads().first().toMap().value("key").toInt() == 55);
    CHECK(page.padName(63) == "Clap" && page.padDrum(63) == "clap" && page.padDrum(56) == "hihatClosed");
    CHECK(page.padName(59).isEmpty() && page.padDrum(59).isEmpty());
    page.setTrackModel(&tracks); page.setTrackId(id);
    QList<QPair<int, int>> hits, played;
    QObject::connect(&page, &ElectroDrumsWrapper::hit, &page, [&hits](int key, int velocity) { hits.append({key, velocity}); });
    QObject::connect(&page, &ElectroDrumsWrapper::played, &page, [&played](int key, int velocity) { played.append({key, velocity}); });

    page.play(63, 90);                                           // no engine yet: nothing
    CHECK(played.isEmpty());
    page.setEngine(&controller);
    page.setWatchHits(true);
    page.play(63, 90);                                           // a click on the clap pad
    CHECK(played.size() == 1 && played[0] == qMakePair(63, 90));
    float peak = keyboard->play(4);
    page.stop(63);
    peak = std::max(peak, keyboard->play(4));
    CHECK(peak > 0.01f);                                         // the kit sounded
    for (int i = 0; i < 10 && hits.isEmpty(); ++i) tick(20);
    CHECK(hits.size() == 1 && hits[0] == qMakePair(63, 90));

    CHECK(tracks.setMonitorEnabled(id, true));                   // a MIDI keyboard's hit lights it too
    tick(); keyboard->play(2);
    keyboard->midi.add({0, 0x90, 60, 110});
    keyboard->play(4);
    for (int i = 0; i < 10 && hits.size() < 2; ++i) tick(20);
    CHECK(hits.size() == 2 && hits[1] == qMakePair(60, 110));

    page.setWatchHits(false);                                    // not watching: no lights
    keyboard->midi.add({0, 0x90, 60, 110});
    keyboard->play(4);
    tick(80);
    CHECK(hits.size() == 2);
    controller.shutdownSession();
}
