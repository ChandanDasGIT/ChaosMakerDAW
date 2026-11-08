#include "testkit.h"
#include "persistence/electrodrumsjson.h"
#include "persistence/projectdocument.h"

#include <functional>

using persistence::electrodrumsjson::fromJson;
using persistence::electrodrumsjson::toJson;
using electrodrums::Drum;

namespace {

electrodrums::Settings sample()
{
    electrodrums::Settings s;
    s.style = electrodrums::Style::Studio;
    s.volumeDb = -2.5;
    s.drum(Drum::Kick) = {-5.0, 1.5, 3.0};
    s.drum(Drum::Clap) = {2.0, 0.5, -6.0};
    s.drum(Drum::Ride) = {12.0, 4.0, -24.0};
    return s;
}

} // namespace

TEST(electrodrumsjson, round_trip_keeps_every_setting)
{
    electrodrums::Settings loaded;
    std::string error;
    CHECK(fromJson(toJson(sample()), loaded, error));
    CHECK(error.empty());
    CHECK(loaded == sample());
    CHECK(fromJson(toJson({}), loaded, error));
    CHECK(loaded == electrodrums::Settings{});
    const auto j = toJson(sample());
    CHECK(j["style"] == "studio" && j["drums"].size() == std::size_t(electrodrums::DrumCount));
    CHECK(j["drums"]["kick"]["tune"] == -5.0);
}

TEST(electrodrumsjson, missing_fields_keep_their_defaults)
{
    electrodrums::Settings loaded = sample();
    std::string error;
    CHECK(fromJson(nlohmann::json::object(), loaded, error));
    CHECK(loaded == electrodrums::Settings{});
    CHECK(fromJson({{"style", "909"}, {"drums", {{"snare", {{"level", -3}}}}}}, loaded, error));
    CHECK(loaded.style == electrodrums::Style::Style909 && loaded.drum(Drum::Snare).levelDb == -3.0);
    CHECK(loaded.drum(Drum::Snare).decay == 1.0 && loaded.drum(Drum::Kick).levelDb == 0.0);
}

TEST(electrodrumsjson, bad_values_are_rejected_and_change_nothing)
{
    const auto good = toJson(sample());
    const auto rejects = [&](const std::function<void(nlohmann::json&)>& spoil) {
        auto bad = good;
        spoil(bad);
        electrodrums::Settings loaded = sample();
        std::string error;
        const bool ok = fromJson(bad, loaded, error);
        return !ok && !error.empty() && loaded == sample();
    };
    CHECK(rejects([](auto& j) { j["style"] = "606"; }));
    CHECK(rejects([](auto& j) { j["style"] = 1; }));
    CHECK(rejects([](auto& j) { j["volume"] = 30; }));
    CHECK(rejects([](auto& j) { j["drums"] = nlohmann::json::array(); }));
    CHECK(rejects([](auto& j) { j["drums"]["cowbell"] = {{"tune", 0}}; }));
    CHECK(rejects([](auto& j) { j["drums"]["kick"] = 3; }));
    CHECK(rejects([](auto& j) { j["drums"]["kick"]["tune"] = 13; }));
    CHECK(rejects([](auto& j) { j["drums"]["snare"]["decay"] = 0.1; }));
    CHECK(rejects([](auto& j) { j["drums"]["clap"]["level"] = "loud"; }));
    electrodrums::Settings loaded;
    std::string error;
    CHECK(!fromJson(nlohmann::json::array(), loaded, error));
}

TEST(electrodrumsjson, project_files_save_and_load_an_electronic_drums_track)
{
    Project project;
    auto& kit = project.addTrack(TrackKind::Instrument, "Beats", "Electronic Drums");
    project.addTrack(TrackKind::Instrument, "Drums", "Drums");
    CHECK(project.setTrackElectroDrums(kit.id, sample()));
    CHECK(!project.setTrackElectroDrums(999, sample()));
    const auto json = persistence::ProjectDocument::serialize(project, {}, {"", ""});
    const auto parsed = nlohmann::json::parse(json);
    CHECK(parsed["project"]["tracks"][0].contains("electrodrums"));
    CHECK(!parsed["project"]["tracks"][1].contains("electrodrums"));   // only Electronic Drums

    persistence::ProjectFileContent loaded;
    std::string error;
    const bool ok = persistence::ProjectDocument::deserialize(json, loaded, error, 48000.0);
    if (!ok) std::printf("    deserialize: %s\n", error.c_str());
    CHECK(ok);
    if (!ok || loaded.project.tracks().empty()) return;
    CHECK(loaded.project.tracks()[0]->electrodrums == sample());

    auto older = parsed;                                            // saved before the settings
    older["project"]["tracks"][0].erase("electrodrums");
    CHECK(persistence::ProjectDocument::deserialize(older.dump(), loaded, error, 48000.0));
    if (!loaded.project.tracks().empty())
        CHECK(loaded.project.tracks()[0]->electrodrums == electrodrums::Settings{});

    auto bad = parsed;
    bad["project"]["tracks"][0]["electrodrums"]["style"] = "606";
    CHECK(!persistence::ProjectDocument::deserialize(bad.dump(), loaded, error, 48000.0));

    auto wild = sample();                                           // clamped, not stored as given
    wild.drum(Drum::Kick).levelDb = 99.0;
    CHECK(project.setTrackElectroDrums(kit.id, wild));
    CHECK(project.findTrack(kit.id)->electrodrums.drum(Drum::Kick).levelDb == electrodrums::MaxLevelDb);
}
