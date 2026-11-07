#include "testkit.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/midi/virtualkeyboardwrap/virtualkeyboardwrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "audiodevice/audiodevice.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QSettings>
#include <QTemporaryDir>

namespace {

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
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-virtualkeyboard-XXXXXX")};
    Storage() {
        if (!QCoreApplication::instance()) {
            static int argc = 1; static char name[] = "virtualkeyboardtests"; static char* argv[]{name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerVirtualKeyboardTests");
        QCoreApplication::setApplicationName("Tests");
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    }
};

} // namespace

TEST(qtvirtualkeyboard, clicked_keys_play_the_monitored_instrument_and_never_hang)
{
    Storage storage;
    auto device = std::make_unique<KeyboardDevice>();
    auto* keyboard = device.get();
    EngineController controller(std::move(device), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int harmonium = tracks.addInstrumentTrackOfType("Harmonium");
    tracks.addInstrumentTrackOfType("Piano");                    // more than one instrument
    CHECK(tracks.setMonitorEnabled(harmonium, true));
    tick(); keyboard->play(2);

    VirtualKeyboardWrapper piano;
    QList<QPair<int, bool>> sent;
    QObject::connect(&piano, &VirtualKeyboardWrapper::noteSent, &piano,
                     [&sent](int note, int, bool on) { sent.append({note, on}); });
    CHECK(piano.noteName(60) == "C4" && piano.noteName(61) == "C#4");

    piano.noteOn(60, 100);                                       // no engine: lights only, no sound
    CHECK(sent.size() == 1 && piano.isHeld(60));
    CHECK(keyboard->play(40) < 1e-6f);
    piano.noteOff(60);

    piano.setEngine(&controller);
    sent.clear();
    piano.noteOn(64, 100);                                       // the harmonium (MON on) sounds
    CHECK(keyboard->play(40) > 1e-3f);
    piano.noteOff(64);
    keyboard->play(1000);                                        // the harmonium's sustain rings out, then nothing
    CHECK(keyboard->play(10) < 1e-4f);
    CHECK(sent == (QList<QPair<int, bool>>{{64, true}, {64, false}}));

    piano.noteOff(64);                                           // not held: nothing more
    piano.noteOn(-1, 100); piano.noteOn(128, 100);               // out of range: ignored
    CHECK(sent.size() == 2);

    piano.noteOn(48, 90);
    piano.noteOn(55, 90);
    sent.clear();
    piano.releaseAll();                                          // closing the window
    CHECK(sent.size() == 2 && !piano.isHeld(48) && !piano.isHeld(55));

    piano.noteOn(52, 90);
    sent.clear();
    piano.setEngine(nullptr);                                    // the old engine gets the note-off
    CHECK(sent == (QList<QPair<int, bool>>{{52, false}}));
    controller.shutdownSession();
}
