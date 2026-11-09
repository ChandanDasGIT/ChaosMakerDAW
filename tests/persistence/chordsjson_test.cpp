#include "testkit.h"
#include "instruments/instrumentfactory.h"
#include "persistence/chordsjson.h"
#include "persistence/projectdocument.h"
#include "session/tracklist.h"

using persistence::ProjectDocument;
using persistence::ProjectFileContent;
using nlohmann::json;

TEST(chordsjson, settings_round_trip_and_bad_ones_are_refused)
{
    chords::Settings s;
    s.chords = {"Am", "G", "F", "E", "", "Dm7", "G/B", "Csus4"};
    s.autoplay = 2;
    s.dynamics = chords::DynamicsTopNote;
    s.bassWithChords = false;
    s.key = {9, true};
    chords::Settings loaded;
    std::string error;
    CHECK(persistence::chordsjson::fromJson(persistence::chordsjson::toJson(s), loaded, error));
    CHECK(loaded == s && error.empty());

    // Spellings are tidied on the way in.
    json tidy = persistence::chordsjson::toJson(s);
    tidy["chords"][0] = "Amin7";
    CHECK(persistence::chordsjson::fromJson(tidy, loaded, error) && loaded.chords[0] == "Am7");
    CHECK(persistence::chordsjson::toJson(s)["dynamics"] == "top" && persistence::chordsjson::toJson(s)["bassWithChords"] == false);
    auto accents = s;
    accents.dynamics = chords::DynamicsMeter;
    CHECK(persistence::chordsjson::toJson(accents)["dynamics"] == "accents");
    CHECK(persistence::chordsjson::fromJson(persistence::chordsjson::toJson(accents), loaded, error) && loaded.dynamics == chords::DynamicsMeter);
    // Older files without the field keep the default (same volume, bass with chords).
    CHECK(persistence::chordsjson::fromJson(json::object(), loaded, error) && loaded == chords::Settings{});
    CHECK(loaded.dynamics == chords::DynamicsEven && loaded.bassWithChords);

    const chords::Settings before = loaded;
    const auto refused = [&](auto edit) {
        json j = persistence::chordsjson::toJson(s);
        edit(j);
        const bool ok = persistence::chordsjson::fromJson(j, loaded, error);
        return !ok && loaded == before && !error.empty();
    };
    CHECK(refused([](json& j) { j["chords"][2] = "H7"; }));
    CHECK(refused([](json& j) { j["chords"] = json::array({"C"}); }));
    CHECK(refused([](json& j) { j["autoplay"] = 7; }));
    CHECK(refused([](json& j) { j["key"] = 12; }));
    CHECK(refused([](json& j) { j["minor"] = "yes"; }));
    CHECK(refused([](json& j) { j["dynamics"] = "loud"; }));
    CHECK(refused([](json& j) { j["dynamics"] = 1; }));
    CHECK(refused([](json& j) { j["bassWithChords"] = "on"; }));
    CHECK(refused([](json& j) { j = json::array(); }));

    // sanitize: autoplay and key into range, an unreadable chord becomes an empty strip.
    chords::Settings odd;
    odd.autoplay = 99; odd.key.tonic = -3; odd.chords[1] = "nonsense";
    const auto clean = chords::sanitize(odd);
    CHECK(clean.autoplay == 4 && clean.key.tonic == 9 && clean.chords[1].empty());
    const auto strips = chords::playerStrips(clean);
    CHECK(strips[0].count == 3 && strips[1].count == 0);
}

TEST(chordsjson, a_chords_track_has_its_strips_then_the_piano_and_saves_both)
{
    // The picker lists Chords beside Vocal and Instrument (not inside the instrument list).
    std::vector<std::string> kinds;
    for (const auto& c : TrackList::trackChoices()) kinds.push_back(c.id);
    CHECK((kinds == std::vector<std::string>{"Vocal", "Instrument", "Chords"}));
    for (const auto& c : TrackList::instrumentChoices()) CHECK(c.id != "Chords");

    TrackList tracks;
    const int id = tracks.addTrackChoice("Chords");
    CHECK(id >= 0);
    const auto* track = tracks.project().findTrack(id);
    CHECK(track && track->kind == TrackKind::Instrument && track->type == "Chords" && track->name == "Chords 1");
    CHECK(track->effects.size() == 2 && track->effects[0].type == "chords" && track->effects[1].type == "piano");
    CHECK(track->chords == chords::Settings{});                     // C major key chords, arpeggio
    CHECK(createInstrument("Chords") && createInstrument("Chords")->typeId() == "chords");

    chords::Settings s;
    s.chords = {"Am", "G", "F", "E", "", "", "", ""};
    s.autoplay = 1;
    CHECK(tracks.setChords(id, s).accepted);
    auto piano = track->piano;
    piano.volumeDb = -4.0;
    CHECK(tracks.setPiano(id, piano).accepted);

    const std::string text = ProjectDocument::serialize(tracks.project(), {}, {""});
    ProjectFileContent loaded;
    std::string error;
    CHECK(ProjectDocument::deserialize(text, loaded, error, 48000.0));
    CHECK(loaded.project.tracks().size() == 1);
    const auto* back = loaded.project.tracks()[0].get();
    CHECK(back->type == "Chords" && back->chords == s && back->piano.volumeDb == -4.0);
    CHECK(back->effects.size() == 2 && back->effects[0].type == "chords");
}
