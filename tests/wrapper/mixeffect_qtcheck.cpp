#include "testkit.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/plugins/mixfx/mixfxwrap/mixeffectwrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "audiodevice/audiodevice.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
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

void tick(int ms = 30) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }

// Throwaway settings and data folders: the tests never touch the real app's files.
struct Storage {
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-mixfx-XXXXXX")};
    Storage() {
        if (!QCoreApplication::instance()) {
            static int argc = 1; static char name[] = "mixfxtests"; static char* argv[]{name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerMixFxTests");
        QCoreApplication::setApplicationName("Tests");
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        qputenv("CHAOSMAKER_SAMPLE_KITS", (dir.path() + "/no-sample-kits").toUtf8());   // never the real kit
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    }
};

} // namespace

TEST(qtmixeffect, the_page_shows_the_specs_cards_and_edits_the_tracks_settings)
{
    TrackListWrapper tracks;
    const int vocal = tracks.addVocalTrack();
    MixEffectWrapper page;
    page.setTrackModel(&tracks);
    page.setTrackId(vocal);
    page.setEffectType("compressor");
    CHECK(page.title() == "Compressor");
    CHECK(page.hasMeter());
    const auto cards = page.cards();
    CHECK(cards.size() == 3);
    QStringList titles;
    int controls = 0;
    for (const auto& card : cards) {
        titles << card.toMap()["title"].toString();
        controls += card.toMap()["controls"].toList().size();
    }
    CHECK((titles == QStringList{"Compressor", "Timing", "Output"}));
    CHECK(controls == 10);
    const auto style = cards[0].toMap()["controls"].toList()[0].toMap();
    CHECK(style["kind"].toString() == "choice");
    CHECK((style["choices"].toStringList() == QStringList{"Clean", "Smooth", "Punchy"}));

    int changes = 0;
    QObject::connect(&page, &MixEffectWrapper::valuesChanged, [&] { ++changes; });
    CHECK(page.values()["ratio"].toDouble() == 4.0);
    CHECK(page.setValue("ratio", 8));
    CHECK(changes == 1);
    CHECK(tracks.mixEffect(vocal, "compressor")->values.at("ratio") == 8.0f);
    CHECK(page.setValue("ratio", 500));                                // clamped by the backend
    CHECK(page.values()["ratio"].toDouble() == 20.0);
    CHECK(!page.setValue("loudness", 1));
    CHECK(page.resetValue("ratio"));
    CHECK(page.values()["ratio"].toDouble() == 4.0);
    page.setBypassed(true);
    CHECK(page.bypassed() && tracks.mixEffect(vocal, "compressor")->bypassed);

    CHECK(page.formatValue("threshold", -18) == QString::fromUtf8("−18.0 dB"));
    CHECK(page.formatValue("ratio", 4) == "4.0:1");
    CHECK(page.formatValue("release", 150) == "150 ms");
    CHECK(page.formatValue("release", 1500) == "1.50 s");
    CHECK(page.formatValue("detectorLowCut", 80) == "80 Hz");
    CHECK(page.formatValue("mix", 100) == "100 %");
    CHECK(page.formatValue("style", 1) == "Smooth");
    CHECK(page.formatValue("autoMakeup", 1) == "On");
    CHECK(page.step("ratio") == 0.1 && page.step("release") == 1.0 && page.step("style") == 1.0);

    page.setEffectType("eq");                                          // not a mixing plugin
    CHECK(page.cards().isEmpty() && !page.setValue("ratio", 2));
}

TEST(qtmixeffect, the_engine_follows_edits_meters_and_chain_imports)
{
    Storage storage;
    EngineController controller(std::make_unique<SilentDevice>(), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int vocal = tracks.addVocalTrack();
    CHECK(tracks.addTrackEffect(vocal, "compressor") >= 0);
    auto monitor = controller.mixEffectProcessor(vocal, "compressor", false);
    auto playback = controller.mixEffectProcessor(vocal, "compressor", true);
    CHECK(monitor && playback && monitor != playback);
    if (!monitor || !playback) return;

    MixEffectWrapper page;
    page.setTrackModel(&tracks);
    page.setTrackId(vocal);
    page.setEffectType("compressor");
    page.setEngine(&controller);
    CHECK(page.setValue("threshold", -30));
    CHECK(monitor->parameter("threshold")->get() == -30.0f);
    CHECK(playback->parameter("threshold")->get() == -30.0f);
    page.setBypassed(true);
    CHECK(playback->parameter("bypass")->get() == 1.0f);
    page.setBypassed(false);

    // The meter: drive the playback compressor hard, then let the page poll.
    playback->prepare(48000, 256);
    std::vector<float> l(256, 0.9f), r(256, 0.9f);
    float* channels[]{l.data(), r.data()};
    for (int i = 0; i < 40; ++i) {
        std::fill(l.begin(), l.end(), (i % 2) ? 0.9f : -0.9f);
        std::fill(r.begin(), r.end(), (i % 2) ? 0.9f : -0.9f);
        ProcessContext ctx;
        ctx.audio = {channels, 2, 256};
        ctx.sampleRate = 48000;
        playback->process(ctx);
    }
    CHECK(controller.mixEffectReductionDb(vocal, "compressor") > 10.0);
    page.setMetering(true);
    for (int i = 0; i < 10 && page.reductionDb() == 0.0; ++i) tick(20);
    CHECK(page.reductionDb() > 10.0);
    page.setMetering(false);
    CHECK(page.reductionDb() == 0.0);

    // A chain file carries the settings to another track, and its processors follow.
    const QUrl file = QUrl::fromLocalFile(storage.dir.path() + "/vocal.chaosfx");
    CHECK(controller.exportEffectChain(vocal, file).value("ok").toBool());
    const int other = tracks.addVocalTrack();
    CHECK(controller.importEffectChain(other, file).value("ok").toBool());
    CHECK(tracks.mixEffect(other, "compressor")->values.at("threshold") == -30.0f);
    auto imported = controller.mixEffectProcessor(other, "compressor", true);
    CHECK(imported && imported->parameter("threshold")->get() == -30.0f);
}

TEST(qtmixeffect, the_master_track_has_effects_the_engine_runs_and_chains_carry)
{
    Storage storage;
    EngineController controller(std::make_unique<SilentDevice>(), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    tracks.addVocalTrack();
    const int master = tracks.masterTrackId();
    int changed = 0;
    QObject::connect(&tracks, &TrackListWrapper::masterChanged, [&] { ++changed; });
    CHECK(tracks.masterEffects().isEmpty());
    CHECK(tracks.addTrackEffect(master, "limiter") > 0);
    CHECK(changed == 1);
    CHECK(tracks.masterEffects().size() == 1 && tracks.masterEffects()[0].toMap()["type"].toString() == "limiter");
    CHECK(tracks.count() == 1);                                   // still one row: the master is not one
    auto limiter = controller.mixEffectProcessor(master, "limiter", true);
    CHECK(limiter != nullptr);

    MixEffectWrapper page;
    page.setTrackModel(&tracks);
    page.setTrackId(master);
    page.setEffectType("limiter");
    CHECK(page.setValue("ceiling", -4));
    CHECK(limiter && limiter->parameter("ceiling")->get() == -4.0f);
    CHECK(tracks.setEqBand(master, 0, 3.0));
    CHECK(tracks.masterEqBands()[0].toDouble() == 3.0);

    const QUrl file = QUrl::fromLocalFile(storage.dir.path() + "/master.chaosfx");
    CHECK(controller.exportEffectChain(master, file).value("ok").toBool());
    CHECK(tracks.removeTrackEffect(master, tracks.masterEffects()[0].toMap()["id"].toInt()));
    CHECK(tracks.masterEffects().isEmpty());
    CHECK(controller.importEffectChain(master, file).value("ok").toBool());
    CHECK(tracks.masterEffects().size() == 1);
    CHECK(tracks.mixEffect(master, "limiter")->values.at("ceiling") == -4.0f);
    CHECK(!tracks.removeTrack(master));
}
