#include "testkit.h"
#include "persistence/drumkitjson.h"
#include "persistence/projectdocument.h"

using persistence::drumkitjson::fromJson;
using persistence::drumkitjson::toJson;

namespace {

drumkit::Settings sample()
{
    drumkit::Settings s;
    s.room = drumkit::Room::Full;
    s.kickDb = -3.0; s.snareDb = 2.5; s.hihatDb = -6.0; s.tomsDb = 1.0; s.volumeDb = -1.5;
    s.tuneSemitones = -2.0; s.micTiming = 0.8; s.kickBeater = 0.25;
    s.snareBody = 0.5; s.hihatTrash = 0.75; s.tomThud = 1.0;
    return s;
}

} // namespace

TEST(drumkitjson, round_trip_keeps_every_setting)
{
    drumkit::Settings loaded;
    std::string error;
    CHECK(fromJson(toJson(sample()), loaded, error));
    CHECK(error.empty());
    CHECK(loaded == sample());
    CHECK(fromJson(toJson({}), loaded, error));
    CHECK(loaded == drumkit::Settings{});
}

TEST(drumkitjson, missing_fields_keep_their_defaults)
{
    drumkit::Settings loaded = sample();
    std::string error;
    CHECK(fromJson(nlohmann::json::object(), loaded, error));
    CHECK(loaded == drumkit::Settings{});
    CHECK(fromJson({{"room", "close"}, {"kick", -4}}, loaded, error));
    CHECK(loaded.room == drumkit::Room::Close && loaded.kickDb == -4.0);
    CHECK(loaded.micTiming == drumkit::DefaultMicTiming);
}

TEST(drumkitjson, bad_values_are_rejected_and_change_nothing)
{
    const auto good = toJson(sample());
    const auto rejects = [&](const char* key, const nlohmann::json& value) {
        auto bad = good;
        bad[key] = value;
        drumkit::Settings loaded = sample();
        std::string error;
        const bool ok = fromJson(bad, loaded, error);
        return !ok && !error.empty() && loaded == sample();
    };
    CHECK(rejects("room", "hall"));
    CHECK(rejects("room", 2));
    CHECK(rejects("kick", 40));
    CHECK(rejects("snare", "loud"));
    CHECK(rejects("tune", 13));
    CHECK(rejects("micTiming", 1.5));
    CHECK(rejects("tomThud", -0.1));
    drumkit::Settings loaded;
    std::string error;
    CHECK(!fromJson(nlohmann::json::array(), loaded, error));
}

TEST(drumkitjson, project_files_save_and_load_a_drums_track)
{
    Project project;
    auto& drums = project.addTrack(TrackKind::Instrument, "Drums", "Drums");
    project.addTrack(TrackKind::Instrument, "Keys", "Piano");
    CHECK(project.setTrackDrumKit(drums.id, sample()));
    CHECK(!project.setTrackDrumKit(999, sample()));
    const auto json = persistence::ProjectDocument::serialize(project, {}, {"", ""});
    const auto parsed = nlohmann::json::parse(json);
    CHECK(parsed["project"]["tracks"][0].contains("drumkit"));
    CHECK(!parsed["project"]["tracks"][1].contains("drumkit"));    // only Drums tracks

    persistence::ProjectFileContent loaded;
    std::string error;
    const bool ok = persistence::ProjectDocument::deserialize(json, loaded, error, 48000.0);
    if (!ok) std::printf("    deserialize: %s\n", error.c_str());
    CHECK(ok);
    if (!ok || loaded.project.tracks().empty()) return;
    CHECK(loaded.project.tracks()[0]->drumkit == sample());

    auto older = parsed;                                            // saved before the kit existed
    older["project"]["tracks"][0].erase("drumkit");
    CHECK(persistence::ProjectDocument::deserialize(older.dump(), loaded, error, 48000.0));
    if (!loaded.project.tracks().empty()) CHECK(loaded.project.tracks()[0]->drumkit == drumkit::Settings{});

    auto bad = parsed;
    bad["project"]["tracks"][0]["drumkit"]["room"] = "cathedral";
    CHECK(!persistence::ProjectDocument::deserialize(bad.dump(), loaded, error, 48000.0));

    auto wild = sample();                                           // clamped, not stored as given
    wild.kickDb = 99.0;
    CHECK(project.setTrackDrumKit(drums.id, wild));
    CHECK(project.findTrack(drums.id)->drumkit.kickDb == drumkit::MaxLevelDb);
}
