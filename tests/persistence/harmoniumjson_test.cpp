#include "testkit.h"
#include "persistence/harmoniumjson.h"
#include "persistence/projectdocument.h"

using persistence::harmoniumjson::fromJson;
using persistence::harmoniumjson::toJson;

namespace {

harmonium::Settings sample()
{
    harmonium::Settings s;
    s.style = harmonium::Style::Western;
    s.indian = {true, false, true};
    s.western = {true, false, true, true, true};
    s.bellows = 0.55; s.pumping = 0.1; s.beatingCents = 6.5; s.airNoise = 0.2;
    s.keyClick = 0.6; s.caseResonance = 0.8; s.transpose = -5; s.volumeDb = -3.0;
    s.sustainSeconds = 2.25;
    s.drones = {48, 55, harmonium::NoDrone, 60};
    return s;
}

} // namespace

TEST(harmoniumjson, round_trip_keeps_every_setting)
{
    harmonium::Settings loaded;
    std::string error;
    CHECK(fromJson(toJson(sample()), loaded, error));
    CHECK(error.empty());
    CHECK(loaded == sample());
    CHECK(fromJson(toJson({}), loaded, error));
    CHECK(loaded == harmonium::Settings{});
}

TEST(harmoniumjson, missing_fields_keep_their_defaults)
{
    harmonium::Settings loaded = sample();
    std::string error;
    CHECK(fromJson(nlohmann::json::object(), loaded, error));
    harmonium::Settings older;                                   // no "sustain": an older file
    older.sustainSeconds = 0.0;
    CHECK(loaded == older);
    CHECK(fromJson({{"indian", {{"female", true}}}, {"bellows", 0.25}}, loaded, error));
    CHECK(loaded.indian.female && loaded.indian.male && !loaded.indian.bass);
    CHECK(loaded.bellows == 0.25 && loaded.pumping == harmonium::Settings{}.pumping);
}

TEST(harmoniumjson, bad_values_are_rejected_and_change_nothing)
{
    const auto good = toJson(sample());
    const auto rejects = [&](const char* key, const nlohmann::json& value) {
        auto bad = good;
        bad[key] = value;
        harmonium::Settings loaded = sample();
        std::string error;
        const bool ok = fromJson(bad, loaded, error);
        return !ok && !error.empty() && loaded == sample();
    };
    CHECK(rejects("style", "baroque"));
    CHECK(rejects("style", 1));
    CHECK(rejects("bellows", 1.5));
    CHECK(rejects("bellows", "loud"));
    CHECK(rejects("beating", -1));
    CHECK(rejects("volume", 40));
    CHECK(rejects("sustain", 4.5));
    CHECK(rejects("sustain", -0.5));
    CHECK(rejects("transpose", 13));
    CHECK(rejects("transpose", 1.5));
    CHECK(rejects("drones", nlohmann::json::array({48, 55})));
    CHECK(rejects("drones", nlohmann::json::array({48, 55, 128, -1})));
    CHECK(rejects("drones", nlohmann::json::array({48, 55, -2, -1})));
    CHECK(rejects("indian", true));
    CHECK(rejects("western", {{"voxHumana", "on"}}));

    harmonium::Settings loaded;
    std::string error;
    CHECK(!fromJson(nlohmann::json::array(), loaded, error));
}

TEST(harmoniumjson, project_files_save_and_load_a_harmonium_track)
{
    Project project;
    auto& harmonium = project.addTrack(TrackKind::Instrument, "Harmonium", "Harmonium");
    project.addTrack(TrackKind::Instrument, "Keys", "Piano");
    CHECK(project.setTrackHarmonium(harmonium.id, sample()));
    CHECK(!project.setTrackHarmonium(999, sample()));
    const auto json = persistence::ProjectDocument::serialize(project, {}, {"", ""});   // effects per track
    const auto parsed = nlohmann::json::parse(json);
    CHECK(parsed["project"]["tracks"][0].contains("harmonium"));
    CHECK(!parsed["project"]["tracks"][1].contains("harmonium"));   // only harmonium tracks

    persistence::ProjectFileContent loaded;
    std::string error;
    const bool ok = persistence::ProjectDocument::deserialize(json, loaded, error, 48000.0);
    if (!ok) std::printf("    deserialize: %s\n", error.c_str());
    CHECK(ok);
    if (!ok || loaded.project.tracks().empty()) return;
    CHECK(loaded.project.tracks()[0]->harmonium == sample());
    CHECK(loaded.project.tracks()[0]->type == "Harmonium");

    auto bad = parsed;
    bad["project"]["tracks"][0]["harmonium"]["bellows"] = 9;
    CHECK(!persistence::ProjectDocument::deserialize(bad.dump(), loaded, error, 48000.0));

    // Out-of-range settings set in code are clamped, not stored as they came.
    auto wild = sample();
    wild.bellows = 4.0;
    CHECK(project.setTrackHarmonium(harmonium.id, wild));
    CHECK(project.findTrack(harmonium.id)->harmonium.bellows == 1.0);
}
