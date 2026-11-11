#include "testkit.h"
#include "wrapper/plugins/denoise/denoisewrap/denoisewrapper.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "audiodevice/audiodevice.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>
#include <cstdint>

// The Noise Reduction page's model (restMap G13).
namespace {
class SilentDevice final : public AudioDevice {
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
    void process() {
        float* in[]{input}; float* out[]{left, right};
        if (callback) callback->process({{in, 1, 128}, {out, 2, 128}, nullptr, 128});
    }
};
struct Storage {
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-denoise-XXXXXX")};
    Storage() {
        if (!QCoreApplication::instance()) {
            static int argc = 1; static char name[] = "denoisetests"; static char* argv[]{name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerDenoiseTests");
        QCoreApplication::setApplicationName("Tests");
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    }
};
void tick(int ms = 30) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }

// 1 s of hiss, then 1 s of a loud tone, as a dry take at 0.
int vocalWithNoise(TrackListWrapper& tracks)
{
    const int vocal = tracks.addVocalTrack();
    auto audio = std::make_shared<AudioData>();
    audio->path = "noisy.wav"; audio->sampleRate = 48000; audio->frames = 96000;
    audio->channels = {std::vector<float>(96000)};
    std::uint32_t seed = 3;
    for (int i = 0; i < 96000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        audio->channels[0][std::size_t(i)] = i < 48000 ? 0.02f * (float(seed >> 8) / float(1u << 24) * 2.0f - 1.0f)
                                                       : 0.5f * float(std::sin(0.13 * i));
    }
    CHECK(tracks.attachTake(vocal, 0, audio, false));
    return vocal;
}
}

TEST(qtdenoise, learning_from_the_selection_draws_the_noise_and_says_why_it_cannot)
{
    Storage storage;
    TrackListWrapper tracks;
    const int vocal = vocalWithNoise(tracks);
    DenoiseWrapper page;
    int profileChanged = 0;
    QObject::connect(&page, &DenoiseWrapper::profileChanged, [&] { ++profileChanged; });
    page.setTrackModel(&tracks);
    page.setEffectsId(vocal);
    CHECK(!page.hasProfile() && page.profile().isEmpty() && page.frequencies().size() == 160);
    CHECK(std::abs(page.frequencies().first().toDouble() - 20.0) < 1e-9 && std::abs(page.frequencies().last().toDouble() - 20000.0) < 1e-6);

    CHECK(!page.learn(1.0, 1.0) && page.message().contains("time selection"));
    CHECK(!page.learn(10.0, 12.0) && page.message().contains("no recorded audio"));
    profileChanged = 0;
    CHECK(page.learn(0.0, 1.5) && page.message().isEmpty());
    CHECK(page.hasProfile() && profileChanged >= 1);
    const auto curve = page.profile();
    CHECK(curve.size() == 160);
    // Hiss at 0.02 peak: well below full scale, well above the floor, everywhere.
    for (const auto& v : curve) CHECK(v.toDouble() < -40.0 && v.toDouble() > -110.0);
    // Saved with the effect's settings, so the track's chain gets it.
    CHECK(tracks.mixEffect(vocal, mixfx::Denoise)->profile.size() == std::size_t(mixfx::DenoiseBins));
    CHECK(page.forget() && !page.hasProfile() && page.profile().isEmpty());
}

TEST(qtdenoise, the_page_shows_the_live_sound_before_and_after_the_effect)
{
    Storage storage;
    auto device = std::make_unique<SilentDevice>(); auto* io = device.get();
    EngineController controller(std::move(device), false);
    controller.setRestoreSessionOnStartup(false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int vocal = vocalWithNoise(tracks);
    CHECK(tracks.addTrackEffect(vocal, "denoise") > 0);
    DenoiseWrapper page;
    page.setTrackModel(&tracks);
    page.setEffectsId(vocal);
    CHECK(page.learn(0.0, 1.5));
    tick(); io->process();                                       // publish the chain
    page.setEngine(&controller);
    page.setAnalyzerEnabled(true);
    page.pollAnalyzer(0.03);
    CHECK(page.input().isEmpty() && page.output().isEmpty());    // nothing played yet
    controller.transportAction("play");
    for (int i = 0; i < 60; ++i) io->process();                  // 7680 samples of hiss
    page.pollAnalyzer(0.03);
    CHECK(page.input().size() == 160 && page.output().size() == 160);
    // The hiss comes out quieter than it goes in (the default 12 dB reduction).
    double in = 0.0, out = 0.0;
    for (int i = 60; i < 140; ++i) { in += page.input()[i].toDouble(); out += page.output()[i].toDouble(); }
    CHECK(out / 80.0 < in / 80.0 - 6.0);
    page.setAnalyzerEnabled(false);
    CHECK(page.input().isEmpty());
    controller.shutdownSession();
}
