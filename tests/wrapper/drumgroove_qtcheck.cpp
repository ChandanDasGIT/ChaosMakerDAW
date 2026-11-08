#include "testkit.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/instruments/drums/groovewrap/drumgroovewrapper.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"
#include "audiodevice/audiodevice.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QVariantMap>

#include <cmath>

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
    // One audio block: the engine takes queued transport commands (a seek) here.
    void process()
    {
        float input[128]{}, left[128]{}, right[128]{};
        float* in[]{input}; float* out[]{left, right};
        if (callback) callback->process({{in, 1, 128}, {out, 2, 128}, nullptr, 128});
    }
};

// Throwaway settings and data folders: the tests never touch the real app's files.
struct Storage {
    QTemporaryDir dir{QStringLiteral("/tmp/chaosmaker-grooves-XXXXXX")};
    Storage() {
        if (!QCoreApplication::instance()) {
            static int argc = 1; static char name[] = "groovetests"; static char* argv[]{name, nullptr};
            static QCoreApplication app(argc, argv);
        }
        CHECK(dir.isValid());
        QCoreApplication::setOrganizationName("ChaosMakerGrooveTests");
        QCoreApplication::setApplicationName("Tests");
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        qputenv("CHAOSMAKER_SAMPLE_KITS", (dir.path() + "/no-sample-kits").toUtf8());   // never the real kit
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    }
};

QStringList names(const QVariantList& grooves)
{
    QStringList out;
    for (const auto& g : grooves) out << g.toMap().value("name").toString();
    return out;
}

} // namespace

TEST(qtdrumgroove, lists_the_project_meter_first_and_adds_a_groove_at_the_bar_start)
{
    Storage storage;
    auto device = std::make_unique<SilentDevice>();
    auto* io = device.get();
    EngineController controller(std::move(device), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    const int drums = tracks.addInstrumentTrackOfType("Electronic Drums");
    const int piano = tracks.addInstrumentTrackOfType("Piano");

    DrumGrooveWrapper page;
    page.setFolder(storage.dir.path() + "/grooves");
    page.setTrackModel(&tracks); page.setTrackId(drums);
    page.setEngine(&controller);
    CHECK(page.valid());

    controller.setMeter(4, 4);
    auto list = page.grooves();
    CHECK(list.size() == 6);
    CHECK(page.projectMeter() == "4/4");
    CHECK(list[0].toMap().value("meter") == "4/4" && list[1].toMap().value("meter") == "4/4");
    CHECK(list[0].toMap().value("matchesProject").toBool() && !list[2].toMap().value("matchesProject").toBool());
    controller.setMeter(7, 8);
    list = page.grooves();
    CHECK(page.projectMeter() == "7/8" && list[0].toMap().value("meter") == "7/8");

    // Under 4/4 a playhead at beat 9 adds at bar 3 (beat 8); four bars of a two-bar groove.
    controller.setMeter(4, 4);
    controller.seekBeat(9.0); io->process();
    CHECK(std::abs(controller.positionBeats() - 9.0) < 1e-3);
    const int clip = page.addToTrack("Rock A", 4);
    CHECK(clip >= 0);
    CHECK(std::abs(tracks.domain().clipStartBeat(drums, clip) - 8.0) < 1e-6);
    CHECK(std::abs(tracks.domain().clipLengthBeats(drums, clip) - 16.0) < 1e-6);
    CHECK(!tracks.domain().clipNotes(drums, clip).empty());
    CHECK(page.lastError().isEmpty());

    // Under 7/8 (3.5 beats a bar) a playhead at beat 5 adds at beat 3.5; two bars of 7/8 = 7.
    controller.setMeter(7, 8);
    controller.seekBeat(5.0); io->process();
    const int seven = page.addToTrack("Groove A (2+2+3)", 2);
    CHECK(seven >= 0);
    CHECK(std::abs(tracks.domain().clipStartBeat(drums, seven) - 3.5) < 1e-6);
    CHECK(std::abs(tracks.domain().clipLengthBeats(drums, seven) - 7.0) < 1e-6);
    CHECK(tracks.undo());                                         // one step takes the clip away
    CHECK(!tracks.domain().findClip(drums, seven));

    CHECK(page.addToTrack("No such groove", 2) == -1 && !page.lastError().isEmpty());
    CHECK(page.addToTrack("Rock A", 0) == -1);

    DrumGrooveWrapper other;                                      // not a drum track
    other.setTrackModel(&tracks); other.setTrackId(piano); other.setEngine(&controller);
    CHECK(!other.valid());
    CHECK(other.addToTrack("Rock A", 1) == -1);
}

TEST(qtdrumgroove, the_selected_clip_becomes_a_groove_that_can_be_renamed_and_deleted)
{
    Storage storage;
    EngineController controller(std::make_unique<SilentDevice>(), false);
    TrackListWrapper tracks;
    controller.attachProject(&tracks);
    controller.setMeter(3, 4);
    const int drums = tracks.addInstrumentTrackOfType("Drums");
    const QString folder = storage.dir.path() + "/grooves";

    DrumGrooveWrapper page;
    page.setFolder(folder);
    page.setTrackModel(&tracks); page.setTrackId(drums);
    page.setEngine(&controller);
    int trackSignals = 0;
    QObject::connect(&page, &DrumGrooveWrapper::trackChanged, &page, [&] { ++trackSignals; });

    CHECK(!page.canSaveClip());
    CHECK(!page.saveSelectedClip("Mine") && !page.lastError().isEmpty());
    const int clip = page.addToTrack("Waltz B", 2);
    CHECK(clip >= 0);
    tracks.selectClip(drums, clip);
    CHECK(trackSignals > 0);
    CHECK(page.canSaveClip());

    CHECK(page.saveSelectedClip("  My waltz "));
    CHECK(QDir(folder).entryList({"*.groove"}).size() == 1);      // a file in the given folder
    auto list = page.grooves();
    CHECK(list.size() == 7);
    QVariantMap mine;
    for (const auto& g : list) if (g.toMap().value("name") == "My waltz") mine = g.toMap();
    CHECK(!mine.isEmpty() && !mine.value("factory").toBool());
    CHECK(mine.value("meter") == "3/4" && mine.value("bars").toInt() == 2);
    CHECK(page.exists("my waltz") && page.exists("Rock A") && !page.exists("Nope"));

    CHECK(!page.saveSelectedClip("my WALTZ"));                    // taken (ignoring case)
    CHECK(page.lastError().contains("already exists"));
    CHECK(page.saveSelectedClip("my WALTZ", true));               // replace when asked
    CHECK(page.grooves().size() == 7);
    CHECK(!page.saveSelectedClip("Waltz A", true));               // never over a factory groove

    // A groove of my own goes on the track like a factory one.
    const int again = page.addToTrack("my waltz", 1);
    CHECK(again >= 0 && std::abs(tracks.domain().clipLengthBeats(drums, again) - 3.0) < 1e-6);

    CHECK(page.renameGroove("my waltz", "Sunday"));
    CHECK(names(page.grooves()).contains("Sunday") && !names(page.grooves()).contains("my WALTZ"));
    CHECK(!page.renameGroove("Rock A", "Mine"));                  // factory: refused
    CHECK(!page.removeGroove("Rock B") && !page.lastError().isEmpty());
    CHECK(page.removeGroove("sunday"));
    CHECK(page.grooves().size() == 6);
    CHECK(QDir(folder).entryList({"*.groove"}).isEmpty());

    // A second page (the other drum track's) reads the same folder.
    DrumGrooveWrapper second;
    second.setFolder(folder);
    CHECK(second.grooves().size() == 6);
}
