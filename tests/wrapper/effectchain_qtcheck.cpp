#include "testkit.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/instruments/guitar/guitarsuiteeditorwrap/guitarsuiteeditorwrapper.h"
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
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-chains-XXXXXX")};
    Storage() {
        if (!QCoreApplication::instance()) {
            static int argc = 1; static char name[] = "chaintests"; static char* argv[]{name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerChainTests");
        QCoreApplication::setApplicationName("Tests");
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        qputenv("CHAOSMAKER_SAMPLE_KITS", (dir.path() + "/no-sample-kits").toUtf8());   // never the real kit
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    }
};

QStringList chain(TrackListWrapper& tracks, int id)
{
    QStringList out;
    for (const auto& slot : tracks.project().findTrack(id)->effects) out << QString::fromStdString(slot.type);
    return out;
}

} // namespace

TEST(qteffectchain, a_chain_exports_and_replaces_another_tracks_chain)
{
    Storage storage;
    EngineController controller(std::make_unique<SilentDevice>(), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int guitar = tracks.addInstrumentTrackOfType("Electric Guitar");
    CHECK(tracks.addTrackEffect(guitar, "chaoseq") >= 0);
    CHECK(tracks.addTrackEffect(guitar, "eq") >= 0);
    CHECK(tracks.setEqBand(guitar, 3, 5.0));
    auto* pedals = qobject_cast<GuitarSuiteModel*>(controller.trackEffects(guitar));
    CHECK(pedals != nullptr);
    if (!pedals) return;
    CHECK(pedals->addPedal("Parametric EQ"));
    for (int i = 0; i < 100 && pedals->busy(); ++i) tick(10);
    const int pedalCount = pedals->rowCount();
    CHECK(pedalCount >= 1);

    const QUrl file = QUrl::fromLocalFile(storage.dir.path() + "/lead.chaosfx");
    const auto exported = controller.exportEffectChain(guitar, file);
    CHECK(exported.value("ok").toBool());
    CHECK((exported.value("kept").toStringList() == QStringList{"GuitarSuite", "Chaos EQ", "Equalizer"}));
    CHECK(QFile::exists(file.toLocalFile()));

    // Preview onto a vocal (which has a Chaos EQ of its own): says what would happen, changes nothing.
    const int vocal = tracks.addVocalTrack();
    CHECK(tracks.addTrackEffect(vocal, "chaoseq") >= 0);
    const auto preview = controller.previewEffectChain(vocal, file);
    CHECK(preview.value("ok").toBool());
    CHECK(preview.value("skipped").toStringList().size() == 1);   // GuitarSuite
    CHECK(preview.value("kept").toStringList().size() == 2);
    CHECK(preview.value("trackType").toString() == "Electric Guitar");
    CHECK((chain(tracks, vocal) == QStringList{"chaoseq"}));

    // Import replaces it: the guitar's EQs in the guitar's order, the band gain with them.
    const auto imported = controller.importEffectChain(vocal, file);
    CHECK(imported.value("ok").toBool());
    CHECK(imported.value("skipped").toStringList().size() == 1);
    CHECK((chain(tracks, vocal) == QStringList{"chaoseq", "eq"}));
    CHECK(tracks.project().findTrack(vocal)->eqGainDb[3] == 5.0f);

    // Onto another guitar: the pedalboard comes too.
    const int second = tracks.addInstrumentTrackOfType("Acoustic Guitar");
    auto* secondPedals = qobject_cast<GuitarSuiteModel*>(controller.trackEffects(second));
    CHECK(secondPedals && secondPedals->rowCount() != pedalCount);
    const auto onGuitar = controller.importEffectChain(second, file);
    CHECK(onGuitar.value("ok").toBool());
    CHECK(onGuitar.value("skipped").toStringList().isEmpty());
    CHECK((chain(tracks, second) == QStringList{"guitarsuite", "chaoseq", "eq"}));
    for (int i = 0; i < 200 && secondPedals && (secondPedals->busy() || secondPedals->rowCount() != pedalCount); ++i) tick(10);
    CHECK(secondPedals && secondPedals->rowCount() == pedalCount);
}

TEST(qteffectchain, bad_files_and_instrument_pages_of_other_instruments_change_nothing)
{
    Storage storage;
    EngineController controller(std::make_unique<SilentDevice>(), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int drums = tracks.addInstrumentTrackOfType("Drums");
    const int electro = tracks.addInstrumentTrackOfType("Electronic Drums");
    CHECK(tracks.addTrackEffect(electro, "eq") >= 0);

    const QUrl junk = QUrl::fromLocalFile(storage.dir.path() + "/junk.chaosfx");
    { QFile f(junk.toLocalFile()); CHECK(f.open(QIODevice::WriteOnly)); f.write("{ not a chain"); }
    auto result = controller.importEffectChain(electro, junk);
    CHECK(!result.value("ok").toBool() && !result.value("error").toString().isEmpty());
    CHECK((chain(tracks, electro) == QStringList{"electrodrums", "eq"}));
    CHECK(!controller.previewEffectChain(electro, QUrl::fromLocalFile(storage.dir.path() + "/missing.chaosfx")).value("ok").toBool());
    CHECK(!controller.importEffectChain(9999, junk).value("ok").toBool());

    // The acoustic kit's page can't go onto the electronic kit: skipped, and the chain is
    // replaced by what applies (nothing removable here), so the EQ goes.
    const QUrl kit = QUrl::fromLocalFile(storage.dir.path() + "/kit.chaosfx");
    CHECK(controller.exportEffectChain(drums, kit).value("ok").toBool());
    result = controller.importEffectChain(electro, kit);
    CHECK(result.value("ok").toBool());
    CHECK(result.value("skipped").toStringList().size() == 1);
    CHECK((chain(tracks, electro) == QStringList{"electrodrums"}));
    CHECK(!controller.exportEffectChain(drums, QUrl("https://example.com/x.chaosfx")).value("ok").toBool());
}
