#include "testkit.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/instruments/harmonium/harmoniumwrap/harmoniumwrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "audiodevice/audiodevice.h"
#include "instruments/harmonium/harmonium.h"

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
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-harmonium-XXXXXX")};
    Storage() {
        if (!QCoreApplication::instance()) {
            static int argc = 1; static char name[] = "harmoniumtests"; static char* argv[]{name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerHarmoniumTests");
        QCoreApplication::setApplicationName("Tests");
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    }
};

} // namespace

TEST(qtharmonium, wrapper_edits_the_track_and_follows_other_panels)
{
    Storage storage;
    TrackListWrapper tracks;
    const int id = tracks.addInstrumentTrackOfType("Harmonium");
    const int piano = tracks.addInstrumentTrackOfType("Piano");
    CHECK(tracks.harmonium(id) != nullptr);
    CHECK(tracks.harmonium(piano) == nullptr);

    HarmoniumWrapper a, b;
    a.setTrackModel(&tracks); a.setTrackId(id);
    b.setTrackModel(&tracks); b.setTrackId(id);
    CHECK(a.valid());
    CHECK(a.style() == "indian" && a.male() && !a.female());
    int changes = 0;
    QObject::connect(&tracks, &TrackListWrapper::harmoniumChanged, &tracks, [&changes] { ++changes; });

    a.setFemale(true);
    a.setBellows(0.5);
    a.setStyle("western");
    a.setStyle("baroque");                                       // unknown: ignored
    a.setTranspose(-3);
    CHECK(changes == 4);
    CHECK(tracks.harmonium(id)->indian.female && tracks.harmonium(id)->bellows == 0.5);
    CHECK(tracks.harmonium(id)->style == harmonium::Style::Western);
    CHECK(b.female() && b.bellows() == 0.5 && b.style() == "western" && b.transpose() == -3);

    a.setBellows(1.0);
    a.setBellows(7.0);                                           // clamped to what is stored
    CHECK(a.bellows() == 1.0 && changes == 5);
    a.setTranspose(99);
    CHECK(a.transpose() == harmonium::MaxTranspose);

    CHECK(a.setDrone(0, 48) && a.setDrone(3, 55));
    CHECK(!a.setDrone(4, 50) && !a.setDrone(1, 128) && !a.setDrone(1, -2));
    CHECK(a.drones() == QVariantList({48, -1, -1, 55}));
    CHECK(a.setDrone(0, -1) && b.drones().first().toInt() == -1);

    CHECK(a.resetToDefaults());
    CHECK(*tracks.harmonium(id) == harmonium::Settings{});

    HarmoniumWrapper other;                                      // not a harmonium track
    other.setTrackModel(&tracks); other.setTrackId(piano);
    CHECK(!other.valid());
    other.setFemale(true);
    CHECK(!other.setDrone(0, 48));
    CHECK(!tracks.setHarmonium(piano, harmonium::Settings{}));
    CHECK(other.styles().size() == 2);
}

TEST(qtharmonium, harmonium_track_plays_the_harmonium_and_edits_reach_it)
{
    Storage storage;
    EngineController controller(std::make_unique<SilentDevice>(), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int id = tracks.addInstrumentTrackOfType("Harmonium");
    const auto instrument = controller.trackInstrument(id);
    CHECK(instrument && instrument->typeId() == "harmonium");
    if (!instrument || instrument->typeId() != "harmonium") return;
    const auto& h = static_cast<const Harmonium&>(*instrument);

    HarmoniumWrapper panel;
    panel.setTrackModel(&tracks); panel.setTrackId(id);
    panel.setBass(true);
    panel.setDrone(1, 50);
    CHECK(h.settings().indian.bass && h.settings().drones[1] == 50);
    CHECK(controller.projectDirty());

    CHECK(controller.setBufferSize(256));                        // a new instrument keeps them
    const auto fresh = controller.trackInstrument(id);
    CHECK(fresh && fresh != instrument && fresh->typeId() == "harmonium");
    if (fresh && fresh->typeId() == "harmonium") {
        const auto& f = static_cast<const Harmonium&>(*fresh);
        CHECK(f.settings().indian.bass && f.settings().drones[1] == 50);
    }
    controller.shutdownSession();
}

TEST(qtharmonium, mon_lets_a_midi_keyboard_play_the_harmonium)
{
    Storage storage;
    auto device = std::make_unique<KeyboardDevice>();
    auto* keyboard = device.get();
    EngineController controller(std::move(device), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int harmonium = tracks.addInstrumentTrackOfType("Harmonium");
    tracks.addInstrumentTrackOfType("Electric Guitar");          // more than one instrument
    const int vocal = tracks.addVocalTrack();
    tick(); keyboard->play(2);

    const auto* track = tracks.project().findTrack(harmonium);
    CHECK(track->canMonitor() && !track->hasLiveInput());       // MIDI, not an audio input
    CHECK(tracks.project().findTrack(vocal)->canMonitor());

    keyboard->midi.add({0, 0x90, 60, 100});                      // nothing armed or monitored
    CHECK(keyboard->play(40) < 1e-6f);                           // ... so the key goes nowhere
    keyboard->midi.add({0, 0x80, 60, 0});
    keyboard->play(40);

    CHECK(tracks.setMonitorEnabled(harmonium, true));
    CHECK(tracks.project().findTrack(harmonium)->monitorEnabled);
    tick(); keyboard->play(2);
    keyboard->midi.add({0, 0x90, 60, 100});                      // transport stopped
    CHECK(keyboard->play(40) > 1e-3f);                           // the harmonium sounds
    keyboard->midi.add({0, 0x80, 60, 0});
    keyboard->play(150);                                         // the reed rings out (~0.28 s to -80 dB)
    CHECK(keyboard->play(10) < 1e-4f);                           // and stops with the key

    CHECK(tracks.setMonitorEnabled(harmonium, false));
    tick(); keyboard->play(2);
    keyboard->midi.add({0, 0x90, 64, 100});
    CHECK(keyboard->play(40) < 1e-6f);
    controller.shutdownSession();
}
