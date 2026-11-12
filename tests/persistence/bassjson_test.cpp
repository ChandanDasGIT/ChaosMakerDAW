#include "testkit.h"
#include "persistence/bassjson.h"
#include "persistence/effectchainjson.h"
#include "persistence/projectdocument.h"
#include "plugins/effectchain.h"
#include "session/tracklist.h"

#include <functional>

using persistence::bassjson::fromJson;
using persistence::bassjson::toJson;
using namespace bass;

namespace {

Settings sample()
{
    Settings s;
    s.style = Style::Slap;
    s.muteAmount = 0.25;
    s.slapOnHardHits = true;
    s.slapVelocity = 96;
    s.keyswitches = false;
    s.pluckPosition = 0.75;
    s.sustain = 0.8;
    s.brightness = 0.3;
    s.velocity = 0.9;
    s.pickupBlend = 1.0;
    s.tone = 0.45;
    s.drive = 0.5;
    s.compression = 0.6;
    s.slideSeconds = 0.25;
    s.bendSemitones = 7;
    s.volumeDb = -4.5;
    return s;
}

} // namespace

TEST(bassjson, round_trip_keeps_every_setting)
{
    Settings back;
    std::string error;
    CHECK(fromJson(toJson(sample()), back, error));
    CHECK(back == sample());
    CHECK(error.empty());
    CHECK(fromJson(toJson(Settings{}), back, error) && back == Settings{});
    const auto j = toJson(sample());
    CHECK(j["style"] == "slap" && j["slide"] == 0.25 && j["bendRange"] == 7);
}

TEST(bassjson, missing_fields_keep_their_defaults)
{
    Settings back = sample();
    std::string error;
    CHECK(fromJson(nlohmann::json::object(), back, error));
    CHECK(back == Settings{});
    CHECK(fromJson(nlohmann::json{{"style", "mute"}, {"tone", 0.2}}, back, error));
    CHECK(back.style == Style::Mute && back.tone == 0.2 && back.pickupBlend == Settings{}.pickupBlend);
}

TEST(bassjson, bad_values_are_rejected_and_change_nothing)
{
    const std::function<void(nlohmann::json&)> breaks[] = {
        [](nlohmann::json& j) { j["style"] = "tapping"; },
        [](nlohmann::json& j) { j["style"] = 1; },
        [](nlohmann::json& j) { j["muteAmount"] = 1.5; },
        [](nlohmann::json& j) { j["slapOnHardHits"] = 1; },
        [](nlohmann::json& j) { j["slapVelocity"] = 0; },
        [](nlohmann::json& j) { j["slapVelocity"] = 64.5; },
        [](nlohmann::json& j) { j["tone"] = "bright"; },
        [](nlohmann::json& j) { j["slide"] = 2.0; },
        [](nlohmann::json& j) { j["bendRange"] = 13; },
        [](nlohmann::json& j) { j["volume"] = -30; },
    };
    for (const auto& breakIt : breaks) {
        auto j = toJson(sample());
        breakIt(j);
        Settings untouched;
        std::string error;
        CHECK(!fromJson(j, untouched, error));
        CHECK(untouched == Settings{});
        CHECK(!error.empty());
    }
    Settings untouched;
    std::string error;
    CHECK(!fromJson(nlohmann::json::array(), untouched, error));
}

TEST(bassjson, project_files_save_and_load_a_bass_guitar_track)
{
    Project project;
    auto& bassTrack = project.addTrack(TrackKind::Instrument, "Bass", "Bass Guitar");
    project.addTrack(TrackKind::Instrument, "Lead", "Synth");
    CHECK(project.setTrackBass(bassTrack.id, sample()));
    CHECK(!project.setTrackBass(999, sample()));
    const auto json = persistence::ProjectDocument::serialize(project, {}, {"", ""});
    const auto parsed = nlohmann::json::parse(json);
    CHECK(parsed["project"]["tracks"][0].contains("bass"));
    CHECK(!parsed["project"]["tracks"][1].contains("bass"));           // only Bass Guitar tracks

    persistence::ProjectFileContent loaded;
    std::string error;
    const bool ok = persistence::ProjectDocument::deserialize(json, loaded, error, 48000.0);
    if (!ok) std::printf("    deserialize: %s\n", error.c_str());
    CHECK(ok);
    if (!ok || loaded.project.tracks().empty()) return;
    CHECK(loaded.project.tracks()[0]->bass == sample());
    CHECK(loaded.project.tracks()[0]->effects.size() == 1 && loaded.project.tracks()[0]->effects[0].type == "bassguitar");

    auto older = parsed;                                               // saved before the settings
    older["project"]["tracks"][0].erase("bass");
    CHECK(persistence::ProjectDocument::deserialize(older.dump(), loaded, error, 48000.0));
    if (!loaded.project.tracks().empty()) CHECK(loaded.project.tracks()[0]->bass == Settings{});

    auto bad = parsed;
    bad["project"]["tracks"][0]["bass"]["style"] = "tapping";
    CHECK(!persistence::ProjectDocument::deserialize(bad.dump(), loaded, error, 48000.0));

    auto wild = sample();                                              // clamped, not stored as given
    wild.tone = 5.0;
    CHECK(project.setTrackBass(bassTrack.id, wild));
    CHECK(project.findTrack(bassTrack.id)->bass.tone == 1.0);
}

TEST(bassjson, a_bass_chain_carries_its_sound_to_another_bass_only)
{
    using namespace persistence;
    TrackList tracks;
    const int bassId = tracks.addInstrumentTrackOfType("Bass Guitar");
    CHECK(tracks.setBass(bassId, sample()).accepted);
    const auto chain = chainjson::capture(*tracks.project().findTrack(bassId));
    CHECK(chain.effects.size() == 1 && chain.effects[0].type == "bassguitar");

    chainjson::ChainFile back;
    std::string error;
    CHECK(chainjson::fromJson(chainjson::toJson(chain), back, error));

    chainjson::ImportPlan plan;
    const int other = tracks.addInstrumentTrackOfType("Bass Guitar");
    CHECK(chainjson::planImport(*tracks.project().findTrack(other), back, plan, error));
    CHECK(plan.bass && *plan.bass == sample());
    CHECK(plan.skipped.empty() && plan.addTypes.empty());

    const int synth = tracks.addInstrumentTrackOfType("Synth");
    CHECK(chainjson::planImport(*tracks.project().findTrack(synth), back, plan, error));
    CHECK(!plan.bass);
    CHECK((plan.skipped == std::vector<std::string>{std::string(effectchain::find("bassguitar")->title)}));

    auto broken = back;
    broken.effects[0].settings["tone"] = -1;
    CHECK(!chainjson::planImport(*tracks.project().findTrack(other), broken, plan, error));
    CHECK(!error.empty());
}
