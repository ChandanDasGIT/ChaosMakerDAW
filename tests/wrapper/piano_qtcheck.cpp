#include "testkit.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/instruments/piano/pianowrap/pianowrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "audiodevice/audiodevice.h"
#include "instruments/piano/piano.h"

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
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-piano-XXXXXX")};
    Storage() {
        if (!QCoreApplication::instance()) {
            static int argc = 1; static char name[] = "pianotests"; static char* argv[]{name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerPianoTests");
        QCoreApplication::setApplicationName("Tests");
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    }
};

} // namespace

TEST(qtpiano, wrapper_edits_the_track_and_follows_other_panels)
{
    Storage storage;
    TrackListWrapper tracks;
    const int id = tracks.addInstrumentTrackOfType("Piano");
    const int strings = tracks.addInstrumentTrackOfType("Strings");
    CHECK(tracks.piano(id) != nullptr && tracks.piano(strings) == nullptr);

    PianoWrapper a, b;
    a.setTrackModel(&tracks); a.setTrackId(id);
    b.setTrackModel(&tracks); b.setTrackId(id);
    CHECK(a.valid());
    CHECK(a.release() == piano::DefaultRelease && a.brightness() == 1.0 && a.transpose() == 0);
    int changes = 0;
    QObject::connect(&tracks, &TrackListWrapper::pianoChanged, &tracks, [&changes] { ++changes; });

    a.setVolume(-3.0);
    a.setRelease(0.9);
    a.setResonance(0.1);
    a.setVelocity(0.4);
    a.setBrightness(0.5);
    a.setTranspose(4);
    CHECK(changes == 6);
    CHECK(tracks.piano(id)->release == 0.9 && tracks.piano(id)->transpose == 4);
    CHECK(b.volume() == -3.0 && b.resonance() == 0.1 && b.velocity() == 0.4 && b.brightness() == 0.5 && b.transpose() == 4);

    a.setTranspose(30);                                          // clamped to what is stored
    CHECK(a.transpose() == piano::MaxTranspose);
    a.setRelease(-1.0);
    CHECK(a.release() == 0.0);

    // The scale changer moves what the on-screen piano shows for the selected track.
    CHECK(tracks.selectTrack(id));
    CHECK(tracks.selectedKeyShift() == piano::MaxTranspose);
    a.setTranspose(-2);
    CHECK(tracks.selectedKeyShift() == -2);

    CHECK(a.resetToDefaults());
    CHECK(*tracks.piano(id) == piano::Settings{});
    CHECK(a.noteName(60) == "C4" && a.lowestKey() == 21 && a.highestKey() == 108);

    PianoWrapper other;                                          // not a Piano track
    other.setTrackModel(&tracks); other.setTrackId(strings);
    CHECK(!other.valid());
    other.setVolume(-3.0);
    CHECK(!other.resetToDefaults());
    CHECK(!tracks.setPiano(strings, piano::Settings{}));
}

TEST(qtpiano, piano_track_plays_the_piano_and_edits_reach_it)
{
    Storage storage;
    EngineController controller(std::make_unique<SilentDevice>(), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int id = tracks.addInstrumentTrackOfType("Piano");
    const auto instrument = controller.trackInstrument(id);
    CHECK(instrument && instrument->typeId() == "piano");
    if (!instrument || instrument->typeId() != "piano") return;
    const auto& keys = static_cast<const Piano&>(*instrument);

    PianoWrapper page;
    page.setTrackModel(&tracks); page.setTrackId(id);
    page.setRelease(0.8);
    page.setTranspose(3);
    CHECK(keys.settings().release == 0.8 && keys.settings().transpose == 3);
    CHECK(controller.projectDirty());

    CHECK(controller.setBufferSize(256));                        // a new instrument keeps them
    const auto fresh = controller.trackInstrument(id);
    CHECK(fresh && fresh != instrument && fresh->typeId() == "piano");
    if (fresh && fresh->typeId() == "piano")
        CHECK(static_cast<const Piano&>(*fresh).settings().transpose == 3);

    // The tests point the sample folder at a path that does not exist: the page says so.
    keys.library()->waitUntilIdle();
    for (int i = 0; i < 20 && !page.missing(); ++i) tick();
    CHECK(page.missing());
    CHECK(page.status().contains("/nonexistent/chaosmaker-tests/sample-kits"));
    controller.shutdownSession();
}

TEST(qtpiano, the_drawn_piano_plays_keys_and_the_pedal_and_sees_every_key)
{
    Storage storage;
    auto device = std::make_unique<KeyboardDevice>();
    auto* keyboard = device.get();
    EngineController controller(std::move(device), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int id = tracks.addInstrumentTrackOfType("Piano");
    tick(); keyboard->play(2);

    PianoWrapper page;
    page.setTrackModel(&tracks); page.setTrackId(id);
    QList<QPair<int, int>> struck, played;
    QObject::connect(&page, &PianoWrapper::struck, &page, [&struck](int key, int velocity) { struck.append({key, velocity}); });
    QObject::connect(&page, &PianoWrapper::played, &page, [&played](int key, int velocity) { played.append({key, velocity}); });

    page.play(60, 90);                                           // no engine yet: nothing
    CHECK(played.isEmpty());
    page.setEngine(&controller);
    page.setWatchKeys(true);
    page.play(60, 90);                                           // a click on middle C
    CHECK(played.size() == 1 && played[0] == qMakePair(60, 90));
    keyboard->play(4);
    for (int i = 0; i < 10 && page.keysDown().isEmpty(); ++i) tick(20);
    CHECK(page.keysDown().value("60").toInt() == 90);
    CHECK(struck.size() == 1 && struck[0] == qMakePair(60, 90));
    page.setPedalDown(true);                                     // the drawn pedal
    keyboard->play(2);
    for (int i = 0; i < 10 && page.pedal() == 0; ++i) tick(20);
    CHECK(page.pedal() == 127);
    page.stop(60);
    page.setPedalDown(false);
    keyboard->play(2);
    for (int i = 0; i < 10 && (!page.keysDown().isEmpty() || page.pedal() != 0); ++i) tick(20);
    CHECK(page.keysDown().isEmpty() && page.pedal() == 0);

    CHECK(tracks.setMonitorEnabled(id, true));                   // a MIDI keyboard's keys and pedal show too
    tick(); keyboard->play(2);
    keyboard->midi.add({0, 0x90, 64, 110});
    keyboard->midi.add({1, 0xB0, 64, 100});
    keyboard->play(4);
    for (int i = 0; i < 10 && struck.size() < 2; ++i) tick(20);
    CHECK(struck.size() == 2 && struck[1] == qMakePair(64, 110));
    CHECK(page.keysDown().contains("64") && page.pedal() == 100);

    page.setWatchKeys(false);                                    // not watching: nothing new
    keyboard->midi.add({0, 0x90, 65, 110});
    keyboard->play(4);
    tick(80);
    CHECK(struck.size() == 2);
    controller.shutdownSession();
}
