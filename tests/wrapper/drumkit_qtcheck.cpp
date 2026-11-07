#include "testkit.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/instruments/drums/drumkitwrap/drumkitwrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "audiodevice/audiodevice.h"
#include "instruments/drums/drumkit.h"

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
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-drumkit-XXXXXX")};
    Storage() {
        if (!QCoreApplication::instance()) {
            static int argc = 1; static char name[] = "drumkittests"; static char* argv[]{name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerDrumKitTests");
        QCoreApplication::setApplicationName("Tests");
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    }
};

} // namespace

TEST(qtdrumkit, wrapper_edits_the_track_and_follows_other_panels)
{
    Storage storage;
    TrackListWrapper tracks;
    const int id = tracks.addInstrumentTrackOfType("Drums");
    const int piano = tracks.addInstrumentTrackOfType("Piano");
    CHECK(tracks.drumKit(id) != nullptr);
    CHECK(tracks.drumKit(piano) == nullptr);

    DrumKitWrapper a, b;
    a.setTrackModel(&tracks); a.setTrackId(id);
    b.setTrackModel(&tracks); b.setTrackId(id);
    CHECK(a.valid());
    CHECK(a.room() == "front" && a.kick() == 0.0 && a.kickBeater() == 1.0);
    int changes = 0;
    QObject::connect(&tracks, &TrackListWrapper::drumKitChanged, &tracks, [&changes] { ++changes; });

    a.setRoom("full");
    a.setRoom("cathedral");                                      // unknown: ignored
    a.setKick(-6.0);
    a.setTune(2.0);
    a.setTomThud(0.5);
    CHECK(changes == 4);
    CHECK(tracks.drumKit(id)->room == drumkit::Room::Full && tracks.drumKit(id)->kickDb == -6.0);
    CHECK(b.room() == "full" && b.kick() == -6.0 && b.tune() == 2.0 && b.tomThud() == 0.5);

    a.setSnare(40.0);                                            // clamped to what is stored
    CHECK(a.snare() == drumkit::MaxLevelDb);
    a.setMicTiming(-1.0);
    CHECK(a.micTiming() == 0.0);

    CHECK(a.resetToDefaults());
    CHECK(*tracks.drumKit(id) == drumkit::Settings{});
    CHECK(a.rooms().size() == 3);

    DrumKitWrapper other;                                        // not a Drums track
    other.setTrackModel(&tracks); other.setTrackId(piano);
    CHECK(!other.valid());
    other.setKick(-3.0);
    CHECK(!other.resetToDefaults());
    CHECK(!tracks.setDrumKit(piano, drumkit::Settings{}));
}

TEST(qtdrumkit, drums_track_plays_the_kit_and_edits_reach_it)
{
    Storage storage;
    EngineController controller(std::make_unique<SilentDevice>(), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int id = tracks.addInstrumentTrackOfType("Drums");
    const auto instrument = controller.trackInstrument(id);
    CHECK(instrument && instrument->typeId() == "drumkit");
    if (!instrument || instrument->typeId() != "drumkit") return;
    const auto& kit = static_cast<const DrumKit&>(*instrument);

    DrumKitWrapper page;
    page.setTrackModel(&tracks); page.setTrackId(id);
    page.setRoom("close");
    page.setHihat(-4.0);
    CHECK(kit.settings().room == drumkit::Room::Close && kit.settings().hihatDb == -4.0);
    CHECK(controller.projectDirty());

    CHECK(controller.setBufferSize(256));                        // a new instrument keeps them
    const auto fresh = controller.trackInstrument(id);
    CHECK(fresh && fresh != instrument && fresh->typeId() == "drumkit");
    if (fresh && fresh->typeId() == "drumkit")
        CHECK(static_cast<const DrumKit&>(*fresh).settings().room == drumkit::Room::Close);

    // The tests point the kit folder at a path that does not exist: the page says so.
    kit.library()->waitUntilIdle();
    for (int i = 0; i < 20 && !page.kitMissing(); ++i) tick();
    CHECK(page.kitMissing());
    CHECK(page.kitStatus().contains("/nonexistent/chaosmaker-tests/sample-kits"));
    controller.shutdownSession();
}

TEST(qtdrumkit, the_picture_plays_parts_and_sees_every_hit)
{
    Storage storage;
    auto device = std::make_unique<KeyboardDevice>();
    auto* keyboard = device.get();
    EngineController controller(std::move(device), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int id = tracks.addInstrumentTrackOfType("Drums");
    tick(); keyboard->play(2);

    DrumKitWrapper page;
    CHECK(page.partKeys().value("kick").toInt() == 60 && page.partKeys().value("hihatOpen").toInt() == 70);
    CHECK(page.partKeys().size() == 13);
    CHECK(page.partName(62) == "Snare" && page.partName(73).isEmpty());
    page.setTrackModel(&tracks); page.setTrackId(id);
    QList<QPair<int, int>> hits, played;
    QObject::connect(&page, &DrumKitWrapper::hit, &page, [&hits](int key, int velocity) { hits.append({key, velocity}); });
    QObject::connect(&page, &DrumKitWrapper::played, &page, [&played](int key, int velocity) { played.append({key, velocity}); });

    page.play(62, 90);                                           // no engine yet: nothing
    CHECK(played.isEmpty());
    page.setEngine(&controller);
    page.setWatchHits(true);
    page.play(62, 90);                                           // a click on the snare
    CHECK(played.size() == 1 && played[0] == qMakePair(62, 90));
    keyboard->play(4);
    page.stop(62);
    keyboard->play(4);
    for (int i = 0; i < 10 && hits.isEmpty(); ++i) tick(20);
    CHECK(hits.size() == 1 && hits[0] == qMakePair(62, 90));

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
