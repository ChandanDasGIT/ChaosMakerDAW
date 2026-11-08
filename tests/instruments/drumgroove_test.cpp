#include "testkit.h"
#include "instruments/drums/drummap.h"
#include "instruments/drums/grooves/drumgroove.h"
#include "persistence/drumgroovejson.h"
#include "persistence/groovestore.h"
#include "session/tracklist.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace fs = std::filesystem;
using drumgroove::Groove;

namespace {

MidiNote note(int key, double beat, int velocity = 100, double length = 0.25)
{
    MidiNote n;
    n.note = key; n.startBeat = beat; n.lengthBeats = length; n.velocity = velocity;
    return n;
}

// A fresh, empty folder under the system temp folder; removed when the test ends.
struct TempFolder {
    fs::path path;
    explicit TempFolder(const char* name)
        : path(fs::temp_directory_path() / (std::string("chaosmaker-grooves-") + name + "-"
               + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        fs::remove_all(path);
    }
    ~TempFolder() { std::error_code ec; fs::remove_all(path, ec); }
};

Groove userGroove(const char* name, int numerator = 4, int denominator = 4)
{
    Groove g;
    g.name = name; g.numerator = numerator; g.denominator = denominator; g.bars = 1;
    g.notes = {note(60, 0, 110), note(62, 1, 100)};
    return g;
}

} // namespace

TEST(drumgroove, two_factory_grooves_for_each_of_three_four_four_four_and_seven_eight)
{
    const auto& grooves = drumgroove::factoryGrooves();
    CHECK(grooves.size() == 6);
    std::map<std::string, int> perMeter;
    std::set<std::string> names;
    for (const auto& g : grooves) {
        perMeter[g.meterText()]++;
        CHECK(g.factory);
        CHECK(names.insert(g.name).second);                       // unique names
        CHECK(g.bars >= 1 && g.bars <= drumgroove::MaxBars);
        CHECK(!g.notes.empty());
        std::set<std::pair<int, double>> hits;
        double previous = -1;
        for (const auto& n : g.notes) {
            CHECK(!drummap::nameOf(n.note).empty());              // a key both kits play
            CHECK(n.note != 73 && n.note != 75);                  // no cymbals: the acoustic kit has none
            CHECK(n.startBeat >= 0 && n.startBeat < g.lengthBeats());
            CHECK(n.lengthBeats > 0 && n.startBeat + n.lengthBeats <= g.lengthBeats() + 1e-9);
            CHECK(n.velocity >= 1 && n.velocity <= 127);
            CHECK(n.startBeat >= previous);                       // in time order
            previous = n.startBeat;
            CHECK(hits.insert({n.note, n.startBeat}).second);     // never the same drum twice at once
        }
        // Every groove starts on a kick and has a snare or side stick: a real beat, not noise.
        CHECK(g.notes.front().startBeat == 0.0);
        CHECK(std::any_of(g.notes.begin(), g.notes.end(), [](const MidiNote& n) { return n.note == 60 && n.startBeat == 0; }));
        CHECK(std::any_of(g.notes.begin(), g.notes.end(), [](const MidiNote& n) { return n.note == 62 || n.note == 61; }));
    }
    CHECK(perMeter["3/4"] == 2 && perMeter["4/4"] == 2 && perMeter["7/8"] == 2);
    // Bars in quarter-note beats: 3/4 = 3, 4/4 = 4, 7/8 = 3.5.
    for (const auto& g : grooves) {
        if (g.meterText() == "3/4") CHECK_NEAR(g.barBeats(), 3.0, 1e-12);
        if (g.meterText() == "4/4") CHECK_NEAR(g.barBeats(), 4.0, 1e-12);
        if (g.meterText() == "7/8") CHECK_NEAR(g.barBeats(), 3.5, 1e-12);
    }
}

TEST(drumgroove, repeated_fills_the_bars_asked_for_and_cuts_the_last_copy)
{
    const Groove* rock = nullptr;
    for (const auto& g : drumgroove::factoryGrooves()) if (g.name == "Rock A") rock = &g;
    CHECK(rock != nullptr);
    if (!rock) return;
    CHECK(rock->bars == 2);
    const auto two = drumgroove::repeated(*rock, 2);
    CHECK(two.size() == rock->notes.size());
    const auto four = drumgroove::repeated(*rock, 4);
    CHECK(four.size() == 2 * rock->notes.size());
    const auto three = drumgroove::repeated(*rock, 3);             // one and a half copies
    for (const auto& n : three) CHECK(n.startBeat < 12.0 && n.startBeat + n.lengthBeats <= 12.0 + 1e-9);
    std::size_t firstBar = 0;
    for (const auto& n : rock->notes) if (n.startBeat < 4.0) ++firstBar;
    CHECK(three.size() == rock->notes.size() + firstBar);
    std::set<int> ids;
    for (const auto& n : four) CHECK(ids.insert(n.id).second);   // ids are fresh and unique
    CHECK(drumgroove::repeated(*rock, 0).empty());
    CHECK(drumgroove::repeated(*rock, drumgroove::MaxRepeatBars + 1).empty());
}

TEST(drumgroove, a_clip_becomes_a_groove_of_whole_bars_under_the_project_meter)
{
    std::string error;
    // 7.6 beats under 7/8 (3.5 a bar) rounds to two bars = 7 beats; a note at 7.2 is left out.
    const std::vector<MidiNote> notes{note(60, 0), note(62, 1.5), note(66, 6.75), note(60, 7.2)};
    const auto g = drumgroove::fromClip("  My 7/8  ", notes, 7.6, 7, 8, error);
    CHECK(!g);                                                     // a slash is not allowed in a name
    const auto ok = drumgroove::fromClip("  My seven  ", notes, 7.6, 7, 8, error);
    CHECK(ok.has_value());
    if (!ok) return;
    CHECK(ok->name == "My seven" && !ok->factory);
    CHECK(ok->numerator == 7 && ok->denominator == 8 && ok->bars == 2);
    CHECK(ok->notes.size() == 3);
    CHECK(!drumgroove::fromClip("Empty", {}, 4, 4, 4, error));
    CHECK(!error.empty());
    CHECK(!drumgroove::fromClip("Long", notes, 4.0 * 17, 4, 4, error));
    CHECK(!drumgroove::fromClip("Odd meter", notes, 4, 5, 3, error));
    const auto short1 = drumgroove::fromClip("Short", {note(60, 0)}, 0.5, 4, 4, error);   // at least one bar
    CHECK(short1 && short1->bars == 1);
}

TEST(drumgroove, groove_files_round_trip_and_bad_ones_are_refused)
{
    const Groove original = drumgroove::factoryGrooves()[4];       // a 7/8 one
    Groove loaded;
    std::string error;
    CHECK(persistence::drumgroovejson::fromJson(persistence::drumgroovejson::toJson(original), loaded, error));
    CHECK(loaded.name == original.name && !loaded.factory);
    CHECK(loaded.numerator == 7 && loaded.denominator == 8 && loaded.bars == original.bars);
    CHECK(loaded.notes.size() == original.notes.size());
    for (std::size_t i = 0; i < loaded.notes.size() && i < original.notes.size(); ++i) {
        CHECK(loaded.notes[i].note == original.notes[i].note);
        CHECK_NEAR(loaded.notes[i].startBeat, original.notes[i].startBeat, 1e-12);
        CHECK(loaded.notes[i].velocity == original.notes[i].velocity);
    }

    auto good = persistence::drumgroovejson::toJson(userGroove("Fine"));
    const auto refused = [&](auto edit) {
        auto j = good;
        edit(j);
        Groove g = loaded;
        const bool ok = persistence::drumgroovejson::fromJson(j, g, error);
        return !ok && g.name == loaded.name && !error.empty();      // unchanged on failure
    };
    CHECK(refused([](auto& j) { j["format"] = "SomethingElse"; }));
    CHECK(refused([](auto& j) { j["version"] = 2; }));
    CHECK(refused([](auto& j) { j["meter"] = {5, 3}; }));
    CHECK(refused([](auto& j) { j["bars"] = 0; }));
    CHECK(refused([](auto& j) { j["notes"] = nlohmann::json::array(); }));
    CHECK(refused([](auto& j) { j["notes"][0]["key"] = 200; }));
    CHECK(refused([](auto& j) { j["notes"][0]["beat"] = 4.0; }));    // past the groove's end
    CHECK(refused([](auto& j) { j["notes"][0]["velocity"] = 0; }));
    CHECK(refused([](auto& j) { j["name"] = "a/b"; }));
    CHECK(refused([](auto& j) { j.erase("notes"); }));
}

TEST(drumgroove, the_store_saves_renames_and_deletes_user_grooves_but_never_factory_ones)
{
    TempFolder folder("store");
    GrooveStore store(folder.path);
    std::string error;
    CHECK(store.all().size() == 6);                                 // the folder need not exist yet
    CHECK(store.userGrooves().empty());

    CHECK(store.save(userGroove("Funk 1"), false, error));
    CHECK(fs::is_directory(folder.path));
    CHECK(store.userGrooves().size() == 1 && store.all().size() == 7);
    CHECK(store.all().back().name == "Funk 1" && !store.all().back().factory);
    CHECK(!store.save(userGroove("funk 1"), false, error));         // names are unique ignoring case
    CHECK(error.find("already exists") != std::string::npos);
    auto changed = userGroove("FUNK 1");
    changed.notes.push_back(note(66, 0.5));
    CHECK(store.save(changed, true, error));                        // replace when asked
    CHECK(store.userGrooves().size() == 1);
    CHECK(store.find("funk 1") && store.find("funk 1")->notes.size() == 3);

    CHECK(!store.save(userGroove("rock a"), false, error));         // a factory name
    CHECK(!store.save(userGroove("Rock A"), true, error));
    CHECK(!store.rename("Rock A", "Mine", error));
    CHECK(!store.remove("Waltz A", error));
    CHECK(store.find("Waltz A") && store.find("Waltz A")->factory);

    CHECK(store.save(userGroove("Seven up", 7, 8), false, error));
    CHECK(store.save(userGroove("Another", 3, 4), false, error));
    const auto users = store.userGrooves();                         // by bar length, then name
    CHECK(users.size() == 3 && users[0].name == "Another" && users[1].name == "Seven up" && users[2].name == "FUNK 1");

    CHECK(!store.rename("Another", "Seven up", error));             // taken
    CHECK(!store.rename("Another", "Rock B", error));               // a factory name
    CHECK(store.rename("Another", "Three", error));
    CHECK(!store.find("Another") && store.find("Three"));
    CHECK(store.remove("three", error));
    CHECK(!store.find("Three") && store.userGrooves().size() == 2);
    CHECK(!store.remove("Three", error));

    // A broken file or a file using a factory name is skipped, not fatal.
    { std::ofstream(folder.path / "broken.groove") << "{ not json"; }
    { std::ofstream(folder.path / "sneaky.groove") << persistence::drumgroovejson::toJson(userGroove("Rock A")).dump(); }
    CHECK(store.userGrooves().size() == 2);
    // A second store on the same folder sees the same grooves (they live on disk).
    CHECK(GrooveStore(folder.path).userGrooves().size() == 2);
}

TEST(drumgroove, a_groove_goes_onto_a_track_as_one_clip_in_one_undo_step)
{
    TrackList tracks;
    const int drums = tracks.addInstrumentTrackOfType("Drums");
    const Groove& waltz = drumgroove::factoryGrooves()[0];
    const auto notes = drumgroove::repeated(waltz, 4);
    const int clip = tracks.addPatternClipAtBeat(drums, 6.0, 4 * waltz.barBeats(), notes);
    CHECK(clip >= 0);
    CHECK_NEAR(tracks.clipStartBeat(drums, clip), 6.0, 1e-9);
    CHECK_NEAR(tracks.clipLengthBeats(drums, clip), 12.0, 1e-6);
    const auto stored = tracks.clipNotes(drums, clip);
    CHECK(stored.size() == notes.size());
    for (std::size_t i = 0; i < stored.size() && i < notes.size(); ++i) {
        CHECK(stored[i].note == notes[i].note);
        CHECK_NEAR(stored[i].startBeat, notes[i].startBeat, 1e-9);
    }
    CHECK(tracks.undo());                                           // one step removes it all
    CHECK(tracks.clips().empty());
    CHECK(tracks.redo());
    CHECK(tracks.clips().size() == 1);

    CHECK(tracks.addPatternClipAtBeat(drums, -1, 4, notes) == -1);
    CHECK(tracks.addPatternClipAtBeat(drums, 0, 0, notes) == -1);
    CHECK(tracks.addPatternClipAtBeat(999, 0, 4, notes) == -1);
    auto bad = notes;
    bad[0].velocity = 300;
    CHECK(tracks.addPatternClipAtBeat(drums, 0, 4, bad) == -1);
}
