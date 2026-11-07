#include "testkit.h"
#include "persistence/chaoseqjson.h"
#include "persistence/projectdocument.h"

using persistence::chaoseqjson::fromJson;
using persistence::chaoseqjson::toJson;

namespace {

chaoseq::Settings sample()
{
    chaoseq::Settings s;
    s.bands[0] = {true, false, false, chaoseq::Placement::Stereo, {eqband::Shape::LowCut, 40.0, 0.0, 0.9, eqband::Slope::Db48}};
    s.bands[7] = {true, true, true, chaoseq::Placement::Side, {eqband::Shape::TiltShelf, 1500.0, -4.5, 0.7, eqband::Slope::Db6}};
    s.bands[23] = {true, false, false, chaoseq::Placement::Mid, {eqband::Shape::Notch, 60.0, 0.0, 12.0, eqband::Slope::Db12}};
    s.outputGainDb = -2.5;
    s.autoGain = true;
    return s;
}

bool same(const chaoseq::Settings& a, const chaoseq::Settings& b)
{
    if (a.outputGainDb != b.outputGainDb || a.autoGain != b.autoGain) return false;
    for (std::size_t i = 0; i < a.bands.size(); ++i) {
        const auto &x = a.bands[i], &y = b.bands[i];
        if (x.inUse != y.inUse || x.bypassed != y.bypassed || x.solo != y.solo || x.placement != y.placement
            || x.filter.shape != y.filter.shape || x.filter.slope != y.filter.slope
            || x.filter.frequencyHz != y.filter.frequencyHz || x.filter.gainDb != y.filter.gainDb
            || x.filter.q != y.filter.q)
            return false;
    }
    return true;
}

} // namespace

TEST(chaoseqjson, round_trip_keeps_every_band_setting)
{
    const auto json = toJson(sample());
    CHECK(json["bands"].size() == 3);   // only bands in use are written
    CHECK(json["bands"][1]["shape"] == "tiltShelf" && json["bands"][1]["placement"] == "side");
    CHECK(json["bands"][0]["slope"] == 48);
    chaoseq::Settings loaded;
    std::string error;
    CHECK(fromJson(json, loaded, error) && error.empty());
    CHECK(same(loaded, sample()));
    // Empty EQ round trips too.
    CHECK(fromJson(toJson({}), loaded, error));
    CHECK(chaoseq::bandsInUse(loaded) == 0);
}

TEST(chaoseqjson, malformed_settings_are_rejected_without_changes)
{
    const auto good = toJson(sample());
    const auto broken = [&](auto change) { auto j = good; change(j); return j; };
    const std::vector<nlohmann::json> cases{
        nlohmann::json::array(),
        broken([](auto& j) { j.erase("bands"); }),
        broken([](auto& j) { j["output"] = 99; }),
        broken([](auto& j) { j["autoGain"] = "yes"; }),
        broken([](auto& j) { j["bands"][0]["slot"] = 24; }),
        broken([](auto& j) { j["bands"][1]["slot"] = 0; }),           // same slot twice
        broken([](auto& j) { j["bands"][0]["shape"] = "wobble"; }),
        broken([](auto& j) { j["bands"][0]["placement"] = "up"; }),
        broken([](auto& j) { j["bands"][0]["slope"] = 18; }),
        broken([](auto& j) { j["bands"][0]["frequency"] = 5.0; }),
        broken([](auto& j) { j["bands"][0]["gain"] = 31.0; }),
        broken([](auto& j) { j["bands"][0]["q"] = 0.0; }),
        broken([](auto& j) { j["bands"][0]["bypass"] = 1; }),
    };
    for (const auto& json : cases) {
        auto kept = sample();
        std::string error;
        CHECK(!fromJson(json, kept, error));
        CHECK(error == "Invalid Chaos EQ settings.");
        CHECK(same(kept, sample()));
    }
}

TEST(chaoseqjson, project_files_save_and_load_each_tracks_chaos_eq)
{
    Project project;
    auto& track = project.addTrack(TrackKind::Audio, "Vox", "Vocal");
    CHECK(project.addTrackEffect(track.id, "chaoseq") > 0);
    CHECK(project.setTrackChaosEq(track.id, sample()));
    const auto json = persistence::ProjectDocument::serialize(project, {}, {""});
    persistence::ProjectFileContent loaded;
    std::string error;
    CHECK(persistence::ProjectDocument::deserialize(json, loaded, error, 48000.0));
    CHECK(same(loaded.project.tracks()[0]->chaosEq, sample()));
    CHECK(loaded.project.tracks()[0]->effects.size() == 1 && loaded.project.tracks()[0]->effects[0].type == "chaoseq");

    // Older files have no "chaosEq": the track loads with an empty Chaos EQ.
    auto old = nlohmann::json::parse(json);
    old["project"]["tracks"][0].erase("chaosEq");
    CHECK(persistence::ProjectDocument::deserialize(old.dump(), loaded, error, 48000.0));
    CHECK(chaoseq::bandsInUse(loaded.project.tracks()[0]->chaosEq) == 0);

    // A bad Chaos EQ block rejects the file.
    auto bad = nlohmann::json::parse(json);
    bad["project"]["tracks"][0]["chaosEq"]["bands"][0]["shape"] = "wobble";
    CHECK(!persistence::ProjectDocument::deserialize(bad.dump(), loaded, error, 48000.0));
    CHECK(error == "Invalid Chaos EQ settings.");
}
