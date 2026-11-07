#include "testkit.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/instruments/harmonium/harmoniumwrap/harmoniumwrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "audiodevice/audiodevice.h"
#include "instruments/harmonium/harmonium.h"

#include <QCoreApplication>
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
