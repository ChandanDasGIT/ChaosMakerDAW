#include "testkit.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/instruments/bass/basswrap/basswrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "audiodevice/audiodevice.h"
#include "instruments/bass/bassguitar.h"
#include "instruments/bass/basspresets.h"

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
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-bass-XXXXXX")};
    Storage() {
        if (!QCoreApplication::instance()) {
            static int argc = 1; static char name[] = "basstests"; static char* argv[]{name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerBassTests");
        QCoreApplication::setApplicationName("Tests");
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    }
};

} // namespace
TEST(qtbass, wrapper_edits_the_track_by_key_and_follows_other_panels)
{
    Storage storage;
    TrackListWrapper tracks;
    const int id = tracks.addInstrumentTrackOfType("Bass Guitar");
    const int piano = tracks.addInstrumentTrackOfType("Piano");
    CHECK(tracks.bass(id) != nullptr);
    CHECK(tracks.bass(piano) == nullptr);

    BassWrapper a, b;
    a.setFolder(storage.dir.path() + "/presets");
    a.setTrackModel(&tracks); a.setTrackId(id);
    b.setTrackModel(&tracks); b.setTrackId(id);
    CHECK(a.valid());
    CHECK(a.values().value("style").toString() == "finger");
    CHECK(a.values().value("keyswitches").toBool() == true);
    CHECK(a.values().value("slapVelocity").toInt() == 110);
    CHECK(a.values().size() == int(bass::controls().size()));
    int changes = 0;
    QObject::connect(&tracks, &TrackListWrapper::bassChanged, &tracks, [&changes] { ++changes; });

    CHECK(a.set("style", "slap"));
    CHECK(a.set("pickupBlend", 0.8));
    CHECK(a.set("slapOnHardHits", true));
    CHECK(a.set("bendRange", 5));
    CHECK(changes == 4);
    CHECK(!a.set("style", "tapping"));                            // not offered
    CHECK(!a.set("nonsense", 1));
    CHECK(!a.set("tone", "loud"));
    CHECK(changes == 4);
    const auto* stored = tracks.bass(id);
    CHECK(stored->style == bass::Style::Slap && stored->pickupBlend == 0.8);
    CHECK(stored->slapOnHardHits && stored->bendSemitones == 5);
    CHECK(b.values().value("style").toString() == "slap" && b.values().value("pickupBlend").toDouble() == 0.8);

    CHECK(a.set("tone", 9.0));                                    // clamped to the range
    CHECK(a.values().value("tone").toDouble() == 1.0);
    CHECK(a.set("slapVelocity", 99.6));                           // rounded
    CHECK(a.values().value("slapVelocity").toInt() == 100);

    const auto controls = a.controls();
    CHECK(controls.value("style").toMap().value("kind").toString() == "choice");
    const auto styles = controls.value("style").toMap().value("choices").toList();
    CHECK(styles.size() == 3 && styles.at(1).toMap().value("title").toString() == "Slap / pop");
    CHECK(controls.value("slide").toMap().value("max").toDouble() == bass::MaxSlideSeconds);
    CHECK(controls.value("keyswitches").toMap().value("kind").toString() == "toggle");

    CHECK(a.resetToDefaults());
    CHECK(*tracks.bass(id) == bass::Settings{});

    BassWrapper other;                                            // not a bass track
    other.setTrackModel(&tracks); other.setTrackId(piano);
    CHECK(!other.valid());
    CHECK(!other.set("tone", 0.5));
    CHECK(!other.loadPreset("Slap"));
    CHECK(!tracks.setBass(piano, bass::Settings{}));
}

TEST(qtbass, presets_load_save_rename_and_delete_in_their_own_folder)
{
    Storage storage;
    TrackListWrapper tracks;
    const int id = tracks.addInstrumentTrackOfType("Bass Guitar");
    BassWrapper page;
    const QString folder = storage.dir.path() + "/bass-presets";
    page.setFolder(folder);
    page.setTrackModel(&tracks); page.setTrackId(id);
    CHECK(page.presets().size() == 6);
    CHECK(page.presets().first().toMap().value("factory").toBool());
    CHECK(page.currentPreset().isEmpty());                         // the defaults are no preset

    CHECK(page.loadPreset("slap"));                                // names ignore case
    CHECK(page.currentPreset() == "Slap");
    CHECK(tracks.bass(id)->style == bass::Style::Slap && *tracks.bass(id) == bass::factoryPresets()[2].settings);
    CHECK(page.set("tone", 0.5));
    CHECK(page.currentPreset().isEmpty());                         // changed: no longer the preset

    CHECK(page.savePreset("My Slap"));
    CHECK(QFile::exists(folder + "/my-slap.basspreset"));
    CHECK(page.presets().size() == 7 && page.currentPreset() == "My Slap");
    CHECK(!page.savePreset("my slap") && page.lastError().contains("already exists"));
    CHECK(page.set("volume", -3.0));
    CHECK(page.savePreset("my slap", true) && page.lastError().isEmpty());
    CHECK(!page.savePreset("Dub", true) && page.lastError().contains("built-in"));
    CHECK(page.isFactoryPreset("Dub") && !page.isFactoryPreset("My Slap"));

    CHECK(page.loadPreset("Dub") && page.currentPreset() == "Dub");
    CHECK(page.loadPreset("My Slap"));
    CHECK(tracks.bass(id)->volumeDb == -3.0 && tracks.bass(id)->tone == 0.5);

    CHECK(!page.renamePreset("Rock Drive", "Rock"));
    CHECK(page.renamePreset("My Slap", "Funk"));
    CHECK(!page.presetExists("My Slap") && page.presetExists("funk") && page.currentPreset() == "Funk");
    CHECK(!page.removePreset("Palm Muted"));
    CHECK(page.removePreset("Funk"));
    CHECK(page.presets().size() == 6 && page.currentPreset().isEmpty());
    CHECK(!page.loadPreset("Funk") && !page.lastError().isEmpty());

    // The default folder is the app data folder's "bass-presets" (here a throwaway one).
    BassWrapper fresh;
    CHECK(fresh.folder().startsWith(storage.dir.path()) && fresh.folder().endsWith("/bass-presets"));
}

TEST(qtbass, bass_track_plays_the_bass_and_edits_reach_it)
{
    Storage storage;
    EngineController controller(std::make_unique<SilentDevice>(), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int id = tracks.addInstrumentTrackOfType("Bass Guitar");
    const auto instrument = controller.trackInstrument(id);
    CHECK(instrument && instrument->typeId() == "bassguitar");
    if (!instrument || instrument->typeId() != "bassguitar") return;
    const auto& b = static_cast<const BassGuitar&>(*instrument);

    BassWrapper page;
    page.setFolder(storage.dir.path() + "/presets");
    page.setTrackModel(&tracks); page.setTrackId(id);
    page.set("style", "mute");
    page.set("pickupBlend", 1.0);
    CHECK(b.settings().style == bass::Style::Mute && b.settings().pickupBlend == 1.0);
    CHECK(controller.projectDirty());

    CHECK(controller.setBufferSize(256));                         // a new instrument keeps them
    const auto fresh = controller.trackInstrument(id);
    CHECK(fresh && fresh != instrument && fresh->typeId() == "bassguitar");
    if (fresh && fresh->typeId() == "bassguitar") {
        const auto& f = static_cast<const BassGuitar&>(*fresh);
        CHECK(f.settings().style == bass::Style::Mute && f.settings().pickupBlend == 1.0);
    }
    controller.shutdownSession();
}

TEST(qtbass, mon_lets_a_midi_keyboard_play_the_bass)
{
    Storage storage;
    auto device = std::make_unique<KeyboardDevice>();
    auto* keyboard = device.get();
    EngineController controller(std::move(device), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int id = tracks.addInstrumentTrackOfType("Bass Guitar");
    tick(); keyboard->play(2);

    CHECK(tracks.setMonitorEnabled(id, true));
    tick(); keyboard->play(2);
    keyboard->midi.add({0, 0x90, 40, 100});
    CHECK(keyboard->play(40) > 1e-3f);                            // the bass sounds
    keyboard->midi.add({0, 0x80, 40, 0});
    keyboard->play(300);                                          // the lifted finger damps it
    CHECK(keyboard->play(10) < 1e-4f);                            // and it stops

    CHECK(tracks.setMonitorEnabled(id, false));
    tick(); keyboard->play(2);
    controller.shutdownSession();
}

TEST(qtbass, a_chain_file_carries_the_bass_sound_to_another_bass_track)
{
    Storage storage;
    EngineController controller(std::make_unique<SilentDevice>(), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int first = tracks.addInstrumentTrackOfType("Bass Guitar");
    const int second = tracks.addInstrumentTrackOfType("Bass Guitar");
    BassWrapper page;
    page.setFolder(storage.dir.path() + "/presets");
    page.setTrackModel(&tracks); page.setTrackId(first);
    CHECK(page.loadPreset("Rock Drive"));

    const auto file = QUrl::fromLocalFile(storage.dir.path() + "/bass.chaosfx");
    const auto exported = controller.exportEffectChain(first, file);
    CHECK(exported.value("ok").toBool());
    const auto imported = controller.importEffectChain(second, file);
    CHECK(imported.value("ok").toBool() && imported.value("skipped").toStringList().isEmpty());
    CHECK(*tracks.bass(second) == bass::factoryPresets().back().settings);
    const auto instrument = controller.trackInstrument(second);
    CHECK(instrument && instrument->typeId() == "bassguitar");
    if (instrument && instrument->typeId() == "bassguitar")
        CHECK(static_cast<const BassGuitar&>(*instrument).settings().drive > 0.4);
    controller.shutdownSession();
}
