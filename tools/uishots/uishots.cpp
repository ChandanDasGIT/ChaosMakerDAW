// uishots: pictures of the app's views, for looking at the UI (before / after a change).
//
// Opens the real main window offscreen with throwaway settings and no audio server, fills it
// with a demo song, and saves a PNG of each view into a folder.
//
//   uishots [<folder>]        default: <source>/renders/ui/shots
//
// Nothing of the user's is read or written: settings and data live in a temporary folder.

#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "core/audiodata.h"
#include "midi/midinote.h"
#include "wrapper/engineWrap/enginecontroller.h"
#include "wrapper/session/tracklistwrap/tracklistwrapper.h"

namespace {

QQuickItem* findIn(QQuickItem* root, const QString& name)
{
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems())
        if (auto* found = findIn(child, name)) return found;
    return nullptr;
}

QList<QQuickWindow*> openWindows()
{
    QList<QQuickWindow*> all;
    for (auto* top : QGuiApplication::topLevelWindows())
        if (auto* quick = qobject_cast<QQuickWindow*>(top); quick && quick->isVisible()) all.append(quick);
    return all;
}

// An item by objectName in any open window (popups and delegates included).
QQuickItem* find(const QString& name)
{
    for (auto* w : openWindows())
        if (auto* found = findIn(w->contentItem(), name)) return found;
    return nullptr;
}

bool click(const QString& name)
{
    auto* item = find(name);
    if (!item || !item->window()) {
        std::fprintf(stderr, "uishots: no item \"%s\"\n", qPrintable(name));
        return false;
    }
    const QPointF centre = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    QTest::mouseClick(item->window(), Qt::LeftButton, Qt::NoModifier, centre.toPoint());
    QTest::qWait(350);   // menus and popups fade in
    return true;
}

struct Shots {
    QQuickWindow* main = nullptr;
    QDir folder;
    int saved = 0;

    // The main window, and every other open window as <name>-window<N>.
    void take(const QString& name)
    {
        QTest::qWait(250);
        save(main, name);
        int n = 0;
        for (auto* w : openWindows())
            if (w != main) save(w, name + "-window" + (n++ > 0 ? QString::number(n) : QString()));
    }
    // Only the windows beside the main one.
    void takeOthers(const QString& name)
    {
        QTest::qWait(250);
        int n = 0;
        for (auto* w : openWindows())
            if (w != main) save(w, name + (n++ > 0 ? "-" + QString::number(n) : QString()));
        if (n == 0) std::fprintf(stderr, "uishots: no window for \"%s\"\n", qPrintable(name));
    }
    void save(QQuickWindow* w, const QString& name)
    {
        const QString path = folder.filePath(name + ".png");
        if (w->grabWindow().save(path)) { ++saved; std::printf("%s\n", qPrintable(path)); }
        else std::fprintf(stderr, "uishots: could not save %s\n", qPrintable(path));
    }
    void closeOthers()
    {
        for (auto* w : openWindows())
            if (w != main) w->close();
        QTest::keyClick(main, Qt::Key_Escape);
        QTest::qWait(250);
    }
};

// A few seconds of something that draws like a recording: notes with a decay, or a voice-like swell.
std::shared_ptr<const AudioData> demoAudio(const char* name, double seconds, double noteHz, double notesPerSecond)
{
    auto data = std::make_shared<AudioData>();
    data->sampleRate = 48000.0;
    data->frames = std::int64_t(seconds * data->sampleRate);
    data->path = name;
    std::vector<float> channel(std::size_t(data->frames));
    unsigned seed = 12345u + unsigned(noteHz);
    for (std::size_t i = 0; i < channel.size(); ++i) {
        const double t = double(i) / data->sampleRate;
        const double inNote = std::fmod(t * notesPerSecond, 1.0);
        const int step = int(t * notesPerSecond);
        const double loud = 0.35 + 0.45 * std::fabs(std::sin(step * 1.7));
        const double pitch = noteHz * (1.0 + 0.25 * ((step * 5) % 4));
        seed = seed * 1664525u + 1013904223u;
        const double noise = (double(seed >> 8) / double(1u << 24)) - 0.5;
        channel[i] = float(loud * std::exp(-3.0 * inNote) * (std::sin(6.283185307 * pitch * t) + 0.15 * noise));
    }
    data->channels = { channel, channel };
    data->buildWaveform();
    return data;
}

std::vector<MidiNote> notes(std::initializer_list<std::array<double, 3>> list)
{
    std::vector<MidiNote> out;
    int id = 1;
    for (const auto& n : list) {
        MidiNote note;
        note.id = id++;
        note.note = int(n[0]);
        note.startBeat = n[1];
        note.lengthBeats = n[2];
        note.velocity = 70 + (id * 13) % 50;
        out.push_back(note);
    }
    return out;
}

std::vector<MidiNote> drumBars(int bars)
{
    std::vector<MidiNote> out;
    int id = 1;
    const auto add = [&](int key, double beat, int velocity) {
        MidiNote n; n.id = id++; n.note = key; n.startBeat = beat; n.lengthBeats = 0.25; n.velocity = velocity; out.push_back(n);
    };
    for (int bar = 0; bar < bars; ++bar) {
        const double b = bar * 4.0;
        add(60, b, 110); add(60, b + 2.5, 95);
        add(62, b + 1, 105); add(62, b + 3, 108);
        for (int e = 0; e < 8; ++e) add(66, b + e * 0.5, e % 2 ? 70 : 95);
    }
    return out;
}

int addTrack(TrackListWrapper& tracks, const char* choice, const char* name)
{
    int id = tracks.addTrackChoice(choice);
    if (id < 0) { std::fprintf(stderr, "uishots: no track choice \"%s\", using an audio track\n", choice); id = tracks.addAudioTrack(); }
    if (name) tracks.renameTrack(id, name);
    return id;
}

struct Demo { int vocal, guitar, drums, bass, piano, synth, drumClip, pianoClip; };

Demo buildDemo(TrackListWrapper& tracks)
{
    Demo d{};
    d.vocal = addTrack(tracks, "Vocal", "Lead Vocal");
    d.guitar = addTrack(tracks, "Electric Guitar", "Rhythm Guitar");
    d.drums = addTrack(tracks, "Drums", nullptr);
    d.bass = addTrack(tracks, "Bass Guitar", nullptr);
    d.piano = addTrack(tracks, "Piano", nullptr);
    d.synth = addTrack(tracks, "Synth", "Pad");
    // Tracks are plain unless the user picks a colour: two get one, to show both looks.
    tracks.setTrackColour(d.drums, "#b18bd9");
    tracks.setTrackColour(d.piano, "#e2a565");

    tracks.importAudio({ demoAudio("Verse take.wav", 12.0, 220.0, 1.5) }, 8.0, d.vocal);
    tracks.importAudio({ demoAudio("Chorus take.wav", 7.0, 262.0, 2.0) }, 36.0, d.vocal);
    tracks.importAudio({ demoAudio("Guitar DI.wav", 22.0, 110.0, 4.0) }, 0.0, d.guitar);

    d.drumClip = tracks.addClipAtBeat(d.drums, 0, 16);
    tracks.setClipNotes(d.drums, d.drumClip, drumBars(4));
    const int drums2 = tracks.addClipAtBeat(d.drums, 16, 32);
    tracks.setClipNotes(d.drums, drums2, drumBars(8));

    const int bass = tracks.addClipAtBeat(d.bass, 0, 32);
    std::vector<MidiNote> line;
    for (int i = 0; i < 64; ++i) {
        MidiNote n; n.id = i + 1; n.note = 40 + (i / 16 % 2 ? 5 : 0) + (i % 8 == 7 ? 3 : 0);
        n.startBeat = i * 0.5; n.lengthBeats = 0.45; n.velocity = 90; line.push_back(n);
    }
    tracks.setClipNotes(d.bass, bass, line);

    d.pianoClip = tracks.addClipAtBeat(d.piano, 8, 16);
    tracks.setClipNotes(d.piano, d.pianoClip, notes({
        {57, 0, 3.5}, {60, 0, 3.5}, {64, 0, 3.5}, {60, 4, 3.5}, {64, 4, 3.5}, {67, 4, 3.5},
        {62, 8, 3.5}, {65, 8, 3.5}, {69, 8, 3.5}, {57, 12, 1.5}, {60, 12, 1.5}, {64, 12, 1.5},
        {72, 2, 0.5}, {71, 2.5, 0.5}, {69, 3, 1}, {76, 6, 1}, {74, 7, 1}, {72, 10, 1.5}, {69, 13.5, 0.5}, {67, 14, 2} }));

    const int pad = tracks.addClipAtBeat(d.synth, 16, 32);
    tracks.setClipNotes(d.synth, pad, notes({ {45, 0, 16}, {52, 0, 16}, {57, 0, 16}, {48, 16, 16}, {55, 16, 16}, {60, 16, 16} }));

    tracks.setVolume(d.vocal, 0.8); tracks.setVolume(d.guitar, 0.6); tracks.setVolume(d.drums, 0.7);
    tracks.setVolume(d.bass, 0.65); tracks.setVolume(d.piano, 0.5); tracks.setVolume(d.synth, 0.4);
    tracks.setMuted(d.synth, true);
    tracks.selectTrack(d.guitar);
    return d;
}

} // namespace

int main(int argc, char* argv[])
{
    // Never a real window, never the real audio server, never the real settings or kits.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    if (!qEnvironmentVariableIsSet("QT_QUICK_BACKEND")) qputenv("QT_QUICK_BACKEND", "software");
    QTemporaryDir storage;
    if (!storage.isValid()) { std::fprintf(stderr, "uishots: no temporary folder\n"); return 1; }
    qputenv("XDG_DATA_HOME", (storage.path() + "/data").toUtf8());
    qputenv("XDG_CONFIG_HOME", (storage.path() + "/config").toUtf8());
    qputenv("CHAOSMAKER_SAMPLE_KITS", (storage.path() + "/no-sample-kits").toUtf8());
    qputenv("PIPEWIRE_REMOTE", "uishots-no-server");
    qputenv("PULSE_SERVER", "unix:/tmp/chaosmaker-uishots-no-server");

    QGuiApplication app(argc, argv);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, storage.path() + "/settings");
    QCoreApplication::setOrganizationName("ChaosMakerShots");
    QCoreApplication::setApplicationName("UiShots");

    Shots shots;
    shots.folder = QDir(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(CHAOSMAKER_SOURCE_DIR "/renders/ui/shots"));
    if (!shots.folder.mkpath(".")) { std::fprintf(stderr, "uishots: cannot make %s\n", qPrintable(shots.folder.path())); return 1; }

    EngineController::instance()->setRestoreSessionOnStartup(false);
    QQmlApplicationEngine engine;
    engine.loadFromModule("UiShots", "Main");
    if (engine.rootObjects().isEmpty()) { std::fprintf(stderr, "uishots: the main window did not load\n"); return 1; }
    shots.main = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    auto* tracks = shots.main ? shots.main->findChild<TrackListWrapper*>() : nullptr;
    if (!tracks) { std::fprintf(stderr, "uishots: no main window or track list\n"); return 1; }
    if (!QTest::qWaitForWindowExposed(shots.main)) std::fprintf(stderr, "uishots: the window was not exposed\n");
    QTest::qWait(400);

    shots.take("01-empty-project");
    click("addTrackButton");
    shots.take("02-add-track");
    shots.closeOthers();

    const Demo demo = buildDemo(*tracks);
    QTest::qWait(600);
    shots.take("03-song-mixer");

    QMetaObject::invokeMethod(shots.main, "openPianoRoll", Q_ARG(QVariant, demo.piano), Q_ARG(QVariant, demo.pianoClip),
                              Q_ARG(QVariant, QString("Piano")), Q_ARG(QVariant, 16.0));
    QTest::qWait(600);
    shots.take("04-song-piano-roll");
    QMetaObject::invokeMethod(shots.main, "openPianoRoll", Q_ARG(QVariant, demo.drums), Q_ARG(QVariant, demo.drumClip),
                              Q_ARG(QVariant, QString("Drums")), Q_ARG(QVariant, 16.0));
    QTest::qWait(600);
    shots.take("05-song-drum-roll");
    QMetaObject::invokeMethod(shots.main, "toggleMixer");

    // Menus.
    for (const auto& [button, name] : { std::pair{"fileMenuButton", "06-menu-file"}, {"editMenuButton", "07-menu-edit"},
                                        {"viewMenuButton", "08-menu-view"}, {"toolsMenuButton", "09-menu-tools"},
                                        {"settingsButton", "10-menu-settings"} }) {
        if (click(button)) shots.take(name);
        shots.closeOthers();
    }
    if (click("quickSettingsButton")) shots.take("11-quick-settings");
    shots.closeOthers();
    if (click("settingsButton") && click("preferencesSetting")) shots.take("12-preferences");
    shots.closeOthers();
    if (click("settingsButton") && click("shortcutsSetting")) shots.take("13-shortcuts");
    shots.closeOthers();

    // Each track's FX window (instrument pages and the guitar suite). The bottom panel is hidden
    // so that every track card is on screen to click.
    shots.main->setProperty("mixerVisible", false);
    QTest::qWait(300);
    shots.take("14-song-timeline-only");
    const std::pair<int, const char*> pages[] = { {demo.guitar, "20-fx-guitar"}, {demo.vocal, "21-fx-vocal"}, {demo.drums, "22-fx-drums"},
                                                  {demo.bass, "23-fx-bass"}, {demo.piano, "24-fx-piano"}, {demo.synth, "25-fx-synth"} };
    for (const auto& [track, name] : pages) {
        if (click("trackFxButton" + QString::number(track))) shots.takeOthers(name);
        if (track == demo.guitar && click("applyGuitarFactoryPreset")) {
            shots.takeOthers("20-fx-guitar-preset");
            // The whole chain in a wide window, the second pedal bypassed (its arc shows).
            if (auto* board = find("guitarPedal1")) {
                QQuickWindow* fx = board->window();
                const int oldWidth = fx->width();
                fx->setWidth(2200);
                QTest::qWait(300);
                click("guitarPedalOn1");
                QTest::qWait(200);
                shots.save(fx, "20-fx-guitar-chain");
                click("guitarPedalOn1");
                fx->setWidth(oldWidth);
                QTest::qWait(150);
            }
        }
        shots.closeOthers();
    }
    // Mixing plugins on the vocal.
    int n = 30;
    for (const char* type : { "eq", "chaoseq", "compressor", "reverb", "chaosverb", "delay", "denoise", "pitchmonitor" }) {
        if (tracks->addTrackEffect(demo.vocal, type) < 0) { std::fprintf(stderr, "uishots: no effect \"%s\"\n", type); continue; }
        // The first effect stays selected when the window opens: pick this one's page.
        if (click("trackFxButton" + QString::number(demo.vocal)) && click(QString("selectEffect_") + type))
            shots.takeOthers(QString::number(n++) + "-fx-" + type);
        shots.closeOthers();
    }

    shots.main->setProperty("mixerVisible", true);

    // Tool windows.
    tracks->selectTrack(demo.guitar);
    if (click("toolsMenuButton") && click("toolsDrumAnalyzer")) shots.takeOthers("40-tool-drum-analyzer");
    shots.closeOthers();
    if (click("toolsMenuButton") && click("toolsChordAnalyzer")) shots.takeOthers("41-tool-chord-analyzer");
    shots.closeOthers();
    if (click("toolsMenuButton") && click("toolsTapTempo")) shots.takeOthers("42-tool-tap-tempo");
    shots.closeOthers();

    // Dialogs inside the main window.
    // The demo song is unsaved, so Open asks first: take that question, then Discard for the picker.
    if (click("fileMenuButton") && click("fileOpenItem")) {
        // A popup, not an item: found as an object; its buttons sit in its footer.
        auto* unsaved = shots.main->findChild<QObject*>("unsavedProjectChanges");
        if (unsaved && unsaved->property("visible").toBool()) {
            shots.take("49-unsaved-changes");
            QQuickItem* discard = nullptr;
            if (auto* footer = qvariant_cast<QQuickItem*>(unsaved->property("footer")))
                for (auto* item : footer->findChildren<QQuickItem*>())
                    if (item->inherits("QQuickAbstractButton") && item->property("text").toString().contains("Discard", Qt::CaseInsensitive))
                        discard = item;
            if (discard) {
                const QPointF centre = discard->mapToScene(QPointF(discard->width() / 2, discard->height() / 2));
                QTest::mouseClick(discard->window(), Qt::LeftButton, Qt::NoModifier, centre.toPoint());
                QTest::qWait(400);
            } else {
                std::fprintf(stderr, "uishots: no Discard button\n");
            }
        }
        shots.take("50-file-picker");
    }
    shots.closeOthers();
    if (click("trackColourButton" + QString::number(demo.vocal))) shots.take("51-track-colour");
    shots.closeOthers();

    std::printf("uishots: %d pictures in %s\n", shots.saved, qPrintable(shots.folder.path()));
    EngineController::instance()->shutdownSession();
    return shots.saved > 0 ? 0 : 1;
}
