#include "testkit.h"
#include "persistence/pianojson.h"
#include "persistence/projectdocument.h"

using persistence::pianojson::fromJson;
using persistence::pianojson::toJson;

namespace {

piano::Settings sample()
{
    piano::Settings s;
    s.volumeDb = -4.5; s.release = 0.8; s.resonance = 0.2; s.velocity = 0.6;
    s.brightness = 0.7; s.transpose = -5;
    return s;
}

} // namespace

TEST(pianojson, round_trip_keeps_every_setting)
{
    piano::Settings loaded;
    std::string error;
    CHECK(fromJson(toJson(sample()), loaded, error));
    CHECK(error.empty() && loaded == sample());
    CHECK(fromJson(toJson({}), loaded, error));
    CHECK(loaded == piano::Settings{});
    CHECK(toJson(sample())["transpose"] == -5);
}

TEST(pianojson, missing_fields_keep_their_defaults)
{
    piano::Settings loaded = sample();
    std::string error;
    CHECK(fromJson(nlohmann::json::object(), loaded, error));
    CHECK(loaded == piano::Settings{});
    CHECK(fromJson({{"transpose", 7}, {"release", 0.1}}, loaded, error));
    CHECK(loaded.transpose == 7 && loaded.release == 0.1 && loaded.resonance == piano::DefaultResonance);
}

TEST(pianojson, bad_values_are_rejected_and_change_nothing)
{
    const auto good = toJson(sample());
    const auto rejects = [&](const char* key, const nlohmann::json& value) {
        auto bad = good;
        bad[key] = value;
        piano::Settings loaded = sample();
        std::string error;
        return !fromJson(bad, loaded, error) && !error.empty() && loaded == sample();
    };
    CHECK(rejects("volume", 30));
    CHECK(rejects("release", 1.5));
    CHECK(rejects("resonance", "lots"));
    CHECK(rejects("velocity", -0.1));
    CHECK(rejects("brightness", 2));
    CHECK(rejects("transpose", 13));
    CHECK(rejects("transpose", 1.5));
    piano::Settings loaded;
    std::string error;
    CHECK(!fromJson(nlohmann::json::array(), loaded, error));
}

TEST(pianojson, project_files_save_and_load_a_piano_track)
{
    Project project;
    auto& keys = project.addTrack(TrackKind::Instrument, "Keys", "Piano");
    project.addTrack(TrackKind::Instrument, "Strings", "Strings");
    CHECK(project.setTrackPiano(keys.id, sample()));
    const auto json = persistence::ProjectDocument::serialize(project, {}, {"", ""});
    const auto parsed = nlohmann::json::parse(json);
    CHECK(parsed["project"]["tracks"][0].contains("piano"));
    CHECK(!parsed["project"]["tracks"][1].contains("piano"));      // only Piano tracks

    persistence::ProjectFileContent loaded;
    std::string error;
    const bool ok = persistence::ProjectDocument::deserialize(json, loaded, error, 48000.0);
    if (!ok) std::printf("    deserialize: %s\n", error.c_str());
    CHECK(ok);
    if (!ok || loaded.project.tracks().empty()) return;
    CHECK(loaded.project.tracks()[0]->piano == sample());
    CHECK(loaded.project.tracks()[0]->keyShift() == -5);

    auto older = parsed;                                            // saved before the piano existed
    older["project"]["tracks"][0].erase("piano");
    CHECK(persistence::ProjectDocument::deserialize(older.dump(), loaded, error, 48000.0));
    if (!loaded.project.tracks().empty()) CHECK(loaded.project.tracks()[0]->piano == piano::Settings{});

    auto bad = parsed;
    bad["project"]["tracks"][0]["piano"]["transpose"] = 40;
    CHECK(!persistence::ProjectDocument::deserialize(bad.dump(), loaded, error, 48000.0));
}
