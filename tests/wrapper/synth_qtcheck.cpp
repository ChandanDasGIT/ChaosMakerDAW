#include "testkit.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/instruments/synth/synthwrap/synthwrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "audiodevice/audiodevice.h"
#include "instruments/synth/synth.h"
#include "instruments/synth/synthpresets.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>

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
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-synth-XXXXXX")};
    Storage() {
        if (!QCoreApplication::instance()) {
            static int argc = 1; static char name[] = "synthtests"; static char* argv[]{name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerSynthTests");
        QCoreApplication::setApplicationName("Tests");
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    }
};

} // namespace

TEST(qtsynth, wrapper_edits_the_track_by_key_and_follows_other_panels)
{
    Storage storage;
    TrackListWrapper tracks;
    const int id = tracks.addInstrumentTrackOfType("Synth");
    const int piano = tracks.addInstrumentTrackOfType("Piano");
    CHECK(tracks.synth(id) != nullptr);
    CHECK(tracks.synth(piano) == nullptr);

    SynthWrapper a, b;
    a.setFolder(storage.dir.path() + "/presets");
    a.setTrackModel(&tracks); a.setTrackId(id);
    b.setTrackModel(&tracks); b.setTrackId(id);
    CHECK(a.valid());
    CHECK(a.values().value("osc1.wave").toString() == "saw");
    CHECK(a.values().value("filter.mode").toString() == "lowpass24");
    CHECK(a.values().value("lfo.sync").toBool() == false);
    CHECK(a.values().size() == int(synth::controls().size()));
    int changes = 0;
    QObject::connect(&tracks, &TrackListWrapper::synthChanged, &tracks, [&changes] { ++changes; });

    CHECK(a.set("osc1.wave", "pulse"));
    CHECK(a.set("filter.cutoff", 800.0));
    CHECK(a.set("lfo.sync", true));
    CHECK(a.set("lfo.division", "3"));
    CHECK(a.set("osc2.octave", 1));
    CHECK(changes == 5);
    CHECK(!a.set("osc1.wave", "square"));                         // not offered
    CHECK(!a.set("nonsense", 1));
    CHECK(!a.set("filter.cutoff", "loud"));
    CHECK(changes == 5);
    const auto* stored = tracks.synth(id);
    CHECK(stored->osc[0].wave == synth::Wave::Pulse && stored->filter.cutoffHz == 800.0);
    CHECK(stored->lfo.sync && stored->lfo.division == 3 && stored->osc[1].octave == 1);
    CHECK(b.values().value("osc1.wave").toString() == "pulse" && b.values().value("filter.cutoff").toDouble() == 800.0);

    CHECK(a.set("filter.cutoff", 1e9));                           // clamped to the range
    CHECK(a.values().value("filter.cutoff").toDouble() == synth::MaxCutoffHz);
    CHECK(a.set("osc2.octave", 2.6));                             // rounded
    CHECK(a.values().value("osc2.octave").toInt() == 3);

    const auto controls = a.controls();
    CHECK(controls.value("filter.mode").toMap().value("kind").toString() == "choice");
    CHECK(controls.value("filter.mode").toMap().value("choices").toList().size() == 4);
    CHECK(controls.value("lfo.division").toMap().value("choices").toList().at(2).toMap().value("title").toString().startsWith("1/4"));
    CHECK(controls.value("filter.cutoff").toMap().value("max").toDouble() == synth::MaxCutoffHz);
    CHECK(controls.value("lfo.sync").toMap().value("kind").toString() == "toggle");

    CHECK(a.resetToDefaults());
    CHECK(*tracks.synth(id) == synth::Settings{});

    SynthWrapper other;                                           // not a synth track
    other.setTrackModel(&tracks); other.setTrackId(piano);
    CHECK(!other.valid());
    CHECK(!other.set("filter.cutoff", 500.0));
    CHECK(!other.loadPreset("Pluck"));
    CHECK(!tracks.setSynth(piano, synth::Settings{}));
}

TEST(qtsynth, presets_load_save_rename_and_delete_in_their_own_folder)
{
    Storage storage;
    TrackListWrapper tracks;
    const int id = tracks.addInstrumentTrackOfType("Synth");
    SynthWrapper page;
    const QString folder = storage.dir.path() + "/synth-presets";
    page.setFolder(folder);
    page.setTrackModel(&tracks); page.setTrackId(id);
    CHECK(page.presets().size() == 8);
    CHECK(page.presets().first().toMap().value("factory").toBool());
    CHECK(page.currentPreset().isEmpty());                         // the defaults are no preset

    CHECK(page.loadPreset("wobble"));                              // names ignore case
    CHECK(page.currentPreset() == "Wobble");
    CHECK(tracks.synth(id)->lfo.sync && *tracks.synth(id) == synth::factoryPresets().back().settings);
    CHECK(page.set("filter.cutoff", 300.0));
    CHECK(page.currentPreset().isEmpty());                         // changed: no longer the preset

    CHECK(page.savePreset("My Wobble"));
    CHECK(QFile::exists(folder + "/my-wobble.synthpreset"));
    CHECK(page.presets().size() == 9 && page.currentPreset() == "My Wobble");
    CHECK(!page.savePreset("my wobble") && page.lastError().contains("already exists"));
    CHECK(page.set("volume", -3.0));
    CHECK(page.savePreset("my wobble", true) && page.lastError().isEmpty());
    CHECK(!page.savePreset("Pluck", true) && page.lastError().contains("built-in"));
    CHECK(page.isFactoryPreset("Pluck") && !page.isFactoryPreset("My Wobble"));

    CHECK(page.loadPreset("Warm Pad") && page.currentPreset() == "Warm Pad");
    CHECK(page.loadPreset("My Wobble"));
    CHECK(tracks.synth(id)->volumeDb == -3.0 && tracks.synth(id)->filter.cutoffHz == 300.0);

    CHECK(!page.renamePreset("Brass", "Horns"));
    CHECK(page.renamePreset("My Wobble", "Dub"));
    CHECK(!page.presetExists("My Wobble") && page.presetExists("dub") && page.currentPreset() == "Dub");
    CHECK(!page.removePreset("Strings"));
    CHECK(page.removePreset("Dub"));
    CHECK(page.presets().size() == 8 && page.currentPreset().isEmpty());
    CHECK(!page.loadPreset("Dub") && !page.lastError().isEmpty());

    // The default folder is the app data folder's "synth-presets" (here a throwaway one).
    SynthWrapper fresh;
    CHECK(fresh.folder().startsWith(storage.dir.path()) && fresh.folder().endsWith("/synth-presets"));
}

TEST(qtsynth, synth_track_plays_the_synth_and_edits_reach_it)
{
    Storage storage;
    EngineController controller(std::make_unique<SilentDevice>(), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int id = tracks.addInstrumentTrackOfType("Synth");
    const auto instrument = controller.trackInstrument(id);
    CHECK(instrument && instrument->typeId() == "synth");
    if (!instrument || instrument->typeId() != "synth") return;
    const auto& s = static_cast<const Synth&>(*instrument);

    SynthWrapper page;
    page.setFolder(storage.dir.path() + "/presets");
    page.setTrackModel(&tracks); page.setTrackId(id);
    page.set("filter.mode", "highpass");
    page.set("lfo.sync", true);
    CHECK(s.settings().filter.mode == synth::FilterMode::HighPass && s.settings().lfo.sync);
    CHECK(controller.projectDirty());

    CHECK(controller.setBufferSize(256));                         // a new instrument keeps them
    const auto fresh = controller.trackInstrument(id);
    CHECK(fresh && fresh != instrument && fresh->typeId() == "synth");
    if (fresh && fresh->typeId() == "synth") {
        const auto& f = static_cast<const Synth&>(*fresh);
        CHECK(f.settings().filter.mode == synth::FilterMode::HighPass && f.settings().lfo.sync);
    }
    controller.shutdownSession();
}

TEST(qtsynth, mon_lets_a_midi_keyboard_play_the_synth)
{
    Storage storage;
    auto device = std::make_unique<KeyboardDevice>();
    auto* keyboard = device.get();
    EngineController controller(std::move(device), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int id = tracks.addInstrumentTrackOfType("Synth");
    tick(); keyboard->play(2);

    CHECK(tracks.setMonitorEnabled(id, true));
    tick(); keyboard->play(2);
    keyboard->midi.add({0, 0x90, 60, 100});
    CHECK(keyboard->play(40) > 1e-3f);                            // the synth sounds
    keyboard->midi.add({0, 0x80, 60, 0});
    keyboard->play(300);                                          // 0.3 s release: 1 % at 0.3 s, silent by 0.6 s
    CHECK(keyboard->play(10) < 1e-4f);                            // and stops

    CHECK(tracks.setMonitorEnabled(id, false));
    tick(); keyboard->play(2);
    controller.shutdownSession();
}

TEST(qtsynth, a_chain_file_carries_the_synth_sound_to_another_synth_track)
{
    Storage storage;
    EngineController controller(std::make_unique<SilentDevice>(), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int lead = tracks.addInstrumentTrackOfType("Synth");
    const int second = tracks.addInstrumentTrackOfType("Synth");
    SynthWrapper page;
    page.setFolder(storage.dir.path() + "/presets");
    page.setTrackModel(&tracks); page.setTrackId(lead);
    CHECK(page.loadPreset("Solo Lead"));

    const auto file = QUrl::fromLocalFile(storage.dir.path() + "/lead.chaosfx");
    const auto exported = controller.exportEffectChain(lead, file);
    CHECK(exported.value("ok").toBool());
    const auto imported = controller.importEffectChain(second, file);
    CHECK(imported.value("ok").toBool() && imported.value("skipped").toStringList().isEmpty());
    CHECK(*tracks.synth(second) == synth::factoryPresets()[1].settings);
    const auto instrument = controller.trackInstrument(second);
    CHECK(instrument && instrument->typeId() == "synth");
    if (instrument && instrument->typeId() == "synth")
        CHECK(static_cast<const Synth&>(*instrument).settings().mode == synth::PlayMode::Legato);
    controller.shutdownSession();
}

TEST(qtsynth, an_unchanged_copy_of_a_preset_shows_the_copy_s_name)
{
    Storage storage;
    TrackListWrapper tracks;
    const int id = tracks.addInstrumentTrackOfType("Synth");
    SynthWrapper page;
    page.setFolder(storage.dir.path() + "/synth-presets");
    page.setTrackModel(&tracks); page.setTrackId(id);
    CHECK(page.loadPreset("Fat Bass") && page.currentPreset() == "Fat Bass");
    CHECK(page.savePreset("  My Bass "));                         // the same sound, saved as a copy
    CHECK(page.currentPreset() == "My Bass");
    CHECK(page.renamePreset("my bass", "Low One") && page.currentPreset() == "Low One");
    CHECK(page.loadPreset("Fat Bass") && page.currentPreset() == "Fat Bass");
    CHECK(page.loadPreset("low one") && page.currentPreset() == "Low One");
}
