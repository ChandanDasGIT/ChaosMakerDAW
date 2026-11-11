#include "testkit.h"
#include "wrapper/plugins/pitchmonitor/pitchmonitorwrap/pitchmonitorwrapper.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "audiodevice/audiodevice.h"
#include "plugins/mixfx/mixfxspec.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>
#include <numbers>

// The Pitch Monitor page's model (restMap G18).
namespace {
class ToneDevice final : public AudioDevice {
public:
    AudioCallback* callback = nullptr;
    bool open(AudioCallback* c, int frames) override { callback = c; c->deviceChanged(48000, frames); return true; }
    void close() override { callback = nullptr; }
    double sampleRate() const override { return 48000; }
    DeviceLatency latency() const override { return {128, 128, 128, 0}; }
    bool setBufferSize(int) override { return true; }
    std::vector<std::string> inputNames() const override { return {"test:input"}; }
    std::vector<std::string> outputNames() const override { return {"test:left", "test:right"}; }
    bool connectInput(const std::string&) override { return true; }
    bool connectOutput(const std::string&, const std::string&) override { return true; }
    std::string lastError() const override { return {}; }
    float input[128]{}, left[128]{}, right[128]{};
    double hz = 0.0;
    long long played = 0;
    void process() {
        for (int i = 0; i < 128; ++i)
            input[i] = hz > 0.0 ? float(0.3 * std::sin(2 * std::numbers::pi * hz * double(played + i) / 48000.0)) : 0.0f;
        played += 128;
        float* in[]{input}; float* out[]{left, right};
        if (callback) callback->process({{in, 1, 128}, {out, 2, 128}, nullptr, 128});
    }
};
struct Storage {
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-pitchmonitor-XXXXXX")};
    Storage() {
        if (!QCoreApplication::instance()) {
            static int argc = 1; static char name[] = "pitchmonitortests"; static char* argv[]{name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerPitchMonitorTests");
        QCoreApplication::setApplicationName("Tests");
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    }
};
void tick(int ms = 30) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
bool settle(PitchMonitorWrapper& page)
{
    tick(300);                                                   // the change's short wait
    for (int i = 0; i < 300 && page.analysing(); ++i) tick(20);
    return !page.analysing();
}

std::shared_ptr<AudioData> sung(double hz, double seconds, const char* path)
{
    auto audio = std::make_shared<AudioData>();
    audio->path = path; audio->sampleRate = 48000; audio->frames = std::int64_t(seconds * 48000);
    audio->channels = {std::vector<float>(std::size_t(audio->frames))};
    for (std::size_t i = 0; i < audio->channels[0].size(); ++i)
        audio->channels[0][i] = float(0.25 * std::sin(2 * std::numbers::pi * hz * double(i) / 48000.0));
    return audio;
}

// The median pitch of the voiced triples in a curve() list, leaving out the first and last
// (curve() adds one point past each edge, so the line can be drawn into the view).
double medianPitch(const QVariantList& list, double* cents = nullptr)
{
    std::vector<std::pair<double, double>> v;
    for (qsizetype i = 3; i + 5 < list.size(); i += 3)
        if (list[i + 1].toDouble() > 0.0) v.emplace_back(list[i + 1].toDouble(), list[i + 2].toDouble());
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    if (cents) *cents = v[v.size() / 2].second;
    return v[v.size() / 2].first;
}
}

TEST(qtpitchmonitor, the_recorded_line_follows_the_clips_the_key_and_the_tempo)
{
    Storage storage;
    TrackListWrapper tracks;                                     // 120 BPM, 48 kHz
    const int vocal = tracks.addVocalTrack();
    CHECK(tracks.attachTake(vocal, 48000, sung(220.0, 1.0, "a3.wav"), false));     // beats 2..4
    CHECK(tracks.attachTake(vocal, 240000, sung(261.63, 0.5, "c4.wav"), false));   // beats 10..11
    CHECK(tracks.addTrackEffect(vocal, "pitchmonitor") > 0);

    PitchMonitorWrapper page;
    page.setTrackModel(&tracks);
    page.setEffectsId(vocal);
    CHECK(!page.hasLine() && page.lowestNote() == 48 && page.highestNote() == 72);   // a default range
    page.setActive(true);                                        // shown: analyses at once
    CHECK(settle(page));
    CHECK(page.hasLine() && page.lowestNote() == 57 && page.highestNote() == 60);
    double cents = 99.0;
    CHECK_NEAR(medianPitch(page.curve(2.2, 3.8), &cents), 57.0, 0.05);
    CHECK(std::abs(cents) < 5.0);                                // chromatic: on A3
    CHECK_NEAR(medianPitch(page.curve(10.1, 10.9)), 60.0, 0.05);
    CHECK(medianPitch(page.curve(4.5, 9.5)) == 0.0);
    CHECK(page.curve(0.0, 12.0, 10).size() <= (10 + 8) * 3);     // thinned (gaps kept)
    CHECK(page.keyName() == "Chromatic" && page.tonicNames().size() == 12 && page.scaleNames().size() == 6);

    // C minor pentatonic (C Eb F G Bb) has no A: A3 is 100 cents under A#3, its nearest in-key note.
    int keyChanges = 0;
    QObject::connect(&page, &PitchMonitorWrapper::keyChanged, [&] { ++keyChanges; });
    auto key = mixfx::defaults(mixfx::PitchMonitor);
    key.values["key"] = 0;
    key.values["scale"] = 4;
    CHECK(tracks.setMixEffect(vocal, "pitchmonitor", key));
    CHECK(keyChanges >= 1 && page.keyName() == "C minor pentatonic" && page.keyTonic() == 0 && page.scale() == 4);
    CHECK(!page.inKey(57) && page.inKey(58) && page.nameOf(58) == "A#3");
    medianPitch(page.curve(2.2, 3.8), &cents);
    CHECK_NEAR(cents, -100.0, 5.0);

    // Moving the first take re-analyses: the line moves with it.
    const int first = tracks.project().findTrack(vocal)->audioClips[0].id;
    CHECK(tracks.moveClipAtBeat(vocal, first, 6.0));
    CHECK(settle(page));
    CHECK(medianPitch(page.curve(2.2, 3.8)) == 0.0);
    CHECK_NEAR(medianPitch(page.curve(6.2, 7.8)), 57.0, 0.05);

    // A tempo change needs no new analysis: the line is kept in song time.
    CHECK(tracks.domain().setTempo(60.0, 4, 4));
    CHECK_NEAR(medianPitch(page.curve(3.1, 3.9)), 57.0, 0.05);   // 144000 samples = beat 3 at 60 BPM
    CHECK_NEAR(medianPitch(page.curve(5.1, 5.4)), 60.0, 0.05);   // 240000 samples = beat 5

    // Hidden, edits wait; shown again, it catches up.
    page.setActive(false);
    CHECK(tracks.moveClipAtBeat(vocal, first, 0.0));
    tick(400);
    CHECK_NEAR(medianPitch(page.curve(3.1, 3.9)), 57.0, 0.05);   // still the old line
    page.setActive(true);
    CHECK(settle(page));
    CHECK_NEAR(medianPitch(page.curve(0.1, 0.9)), 57.0, 0.05);
}

TEST(qtpitchmonitor, the_live_input_gives_the_readout_and_a_line_while_the_song_runs)
{
    Storage storage;
    auto device = std::make_unique<ToneDevice>(); auto* io = device.get();
    EngineController controller(std::move(device), false);
    controller.setRestoreSessionOnStartup(false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int vocal = tracks.addVocalTrack();
    CHECK(tracks.setTrackInputPort(vocal, "test:input"));
    CHECK(tracks.setMonitorEnabled(vocal, true));
    CHECK(tracks.addTrackEffect(vocal, "pitchmonitor") > 0);
    tick(); io->process();                                       // publish the chain
    CHECK(controller.monitoredTracks().size() == 1);

    PitchMonitorWrapper page;
    page.setTrackModel(&tracks);
    page.setEffectsId(vocal);
    page.setEngine(&controller);
    page.setActive(true);
    io->hz = 220.0;
    for (int i = 0; i < 40; ++i) io->process();                  // stopped: a readout, no line
    page.poll();
    CHECK(page.hasPitch() && page.note() == 57 && page.noteName() == "A3" && std::abs(page.cents()) < 5.0);
    CHECK(page.liveCount() == 0);

    controller.transportAction("play");
    for (int i = 0; i < 200; ++i) io->process();                 // about half a second
    page.poll();
    CHECK(page.liveCount() > 30);
    CHECK_NEAR(medianPitch(page.live(0.0, 2.0)), 57.0, 0.1);

    io->hz = 0.0;                                                // the voice stops: the readout lets go
    for (int i = 0; i < 40; ++i) io->process();
    tick(PitchMonitorWrapper::HoldMs + 50);
    page.poll();
    CHECK(!page.hasPitch() && page.noteName().isEmpty());
    page.setActive(false);
    controller.shutdownSession();
}
