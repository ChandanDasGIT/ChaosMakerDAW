#include "testkit.h"
#include "persistence/effectchainjson.h"
#include "persistence/projectdocument.h"
#include "persistence/synthjson.h"
#include "plugins/effectchain.h"
#include "session/tracklist.h"

#include <functional>

using persistence::synthjson::fromJson;
using persistence::synthjson::toJson;
using namespace synth;

namespace {

Settings sample()
{
    Settings s;
    s.osc[0] = {true, Wave::Pulse, -1, 7, 12.5, 0.25, 0.5};
    s.osc[1] = {true, Wave::Triangle, 2, -3, -25.0, 0.75, 0.25};
    s.subWave = SubWave::Square;
    s.subLevel = 0.5;
    s.noiseLevel = 0.1;
    s.filter = {FilterMode::BandPass, 1500.0, 0.7, 0.3, 1.0, -3.0, 0.6};
    s.filterEnv = {0.2, 0.5, 0.1, 1.5};
    s.ampEnv = {0.05, 1.0, 0.75, 2.0};
    s.ampVelocity = 0.2;
    s.lfo = {LfoShape::Random, LfoTarget::PulseWidth, 2.5, true, 5, 0.4, 1.25};
    s.mode = PlayMode::Legato;
    s.glideSeconds = 0.15;
    s.bendSemitones = 12;
    s.volumeDb = -6.0;
    return s;
}

} // namespace

TEST(synthjson, round_trip_keeps_every_setting)
{
    Settings loaded;
    std::string error;
    CHECK(fromJson(toJson(sample()), loaded, error));
    CHECK(error.empty());
    CHECK(loaded == sample());
    CHECK(fromJson(toJson({}), loaded, error));
    CHECK(loaded == Settings{});
    const auto j = toJson(sample());
    CHECK(j["osc1"]["wave"] == "pulse" && j["osc2"]["octave"] == 2);
    CHECK(j["filter"]["mode"] == "bandpass" && j["lfo"]["sync"] == true && j["lfo"]["division"] == 5);
    CHECK(j["mode"] == "legato" && j["sub"]["wave"] == "square");
}

TEST(synthjson, missing_fields_keep_their_defaults)
{
    Settings loaded = sample();
    std::string error;
    CHECK(fromJson(nlohmann::json::object(), loaded, error));
    CHECK(loaded == Settings{});
    CHECK(fromJson({{"filter", {{"cutoff", 800}}}, {"lfo", {{"sync", true}}}}, loaded, error));
    CHECK(loaded.filter.cutoffHz == 800.0 && loaded.lfo.sync);
    CHECK(loaded.filter.mode == Filter{}.mode && loaded.lfo.division == Lfo{}.division);
    CHECK(loaded.osc == Settings{}.osc);
}

TEST(synthjson, bad_values_are_rejected_and_change_nothing)
{
    const auto good = toJson(sample());
    const auto rejects = [&](const std::function<void(nlohmann::json&)>& spoil) {
        auto bad = good;
        spoil(bad);
        Settings loaded = sample();
        std::string error;
        const bool ok = fromJson(bad, loaded, error);
        return !ok && !error.empty() && loaded == sample();
    };
    CHECK(rejects([](auto& j) { j["osc1"]["wave"] = "square"; }));
    CHECK(rejects([](auto& j) { j["osc1"]["octave"] = 4; }));
    CHECK(rejects([](auto& j) { j["osc2"]["semitones"] = 1.5; }));
    CHECK(rejects([](auto& j) { j["osc2"]["on"] = 1; }));
    CHECK(rejects([](auto& j) { j["osc2"] = 3; }));
    CHECK(rejects([](auto& j) { j["sub"]["wave"] = "saw"; }));
    CHECK(rejects([](auto& j) { j["noise"] = 2; }));
    CHECK(rejects([](auto& j) { j["filter"]["mode"] = "notch"; }));
    CHECK(rejects([](auto& j) { j["filter"]["cutoff"] = 5; }));
    CHECK(rejects([](auto& j) { j["filter"]["envAmount"] = 7; }));
    CHECK(rejects([](auto& j) { j["ampEnv"]["attack"] = 0; }));
    CHECK(rejects([](auto& j) { j["filterEnv"]["sustain"] = "half"; }));
    CHECK(rejects([](auto& j) { j["lfo"]["rate"] = 50; }));
    CHECK(rejects([](auto& j) { j["lfo"]["division"] = 7; }));
    CHECK(rejects([](auto& j) { j["lfo"]["target"] = "pan"; }));
    CHECK(rejects([](auto& j) { j["lfo"] = nlohmann::json::array(); }));
    CHECK(rejects([](auto& j) { j["mode"] = "duo"; }));
    CHECK(rejects([](auto& j) { j["bendRange"] = 25; }));
    CHECK(rejects([](auto& j) { j["volume"] = 30; }));
    Settings loaded;
    std::string error;
    CHECK(!fromJson(nlohmann::json::array(), loaded, error));
}

TEST(synthjson, project_files_save_and_load_a_synth_track)
{
    Project project;
    auto& synthTrack = project.addTrack(TrackKind::Instrument, "Lead", "Synth");
    project.addTrack(TrackKind::Instrument, "Harmonium", "Harmonium");
    CHECK(project.setTrackSynth(synthTrack.id, sample()));
    CHECK(!project.setTrackSynth(999, sample()));
    const auto json = persistence::ProjectDocument::serialize(project, {}, {"", ""});
    const auto parsed = nlohmann::json::parse(json);
    CHECK(parsed["project"]["tracks"][0].contains("synth"));
    CHECK(!parsed["project"]["tracks"][1].contains("synth"));         // only Synth tracks

    persistence::ProjectFileContent loaded;
    std::string error;
    const bool ok = persistence::ProjectDocument::deserialize(json, loaded, error, 48000.0);
    if (!ok) std::printf("    deserialize: %s\n", error.c_str());
    CHECK(ok);
    if (!ok || loaded.project.tracks().empty()) return;
    CHECK(loaded.project.tracks()[0]->synth == sample());
    CHECK(loaded.project.tracks()[0]->effects.size() == 1 && loaded.project.tracks()[0]->effects[0].type == "synth");

    auto older = parsed;                                              // saved before the settings
    older["project"]["tracks"][0].erase("synth");
    CHECK(persistence::ProjectDocument::deserialize(older.dump(), loaded, error, 48000.0));
    if (!loaded.project.tracks().empty()) CHECK(loaded.project.tracks()[0]->synth == Settings{});

    auto bad = parsed;
    bad["project"]["tracks"][0]["synth"]["mode"] = "duo";
    CHECK(!persistence::ProjectDocument::deserialize(bad.dump(), loaded, error, 48000.0));

    auto wild = sample();                                             // clamped, not stored as given
    wild.filter.cutoffHz = 1e9;
    CHECK(project.setTrackSynth(synthTrack.id, wild));
    CHECK(project.findTrack(synthTrack.id)->synth.filter.cutoffHz == MaxCutoffHz);
}

TEST(synthjson, a_synth_chain_carries_its_sound_to_another_synth_only)
{
    using namespace persistence;
    TrackList tracks;
    const int lead = tracks.addInstrumentTrackOfType("Synth");
    CHECK(tracks.setSynth(lead, sample()).accepted);
    const auto chain = chainjson::capture(*tracks.project().findTrack(lead));
    CHECK(chain.effects.size() == 1 && chain.effects[0].type == "synth");

    chainjson::ChainFile back;
    std::string error;
    CHECK(chainjson::fromJson(chainjson::toJson(chain), back, error));

    chainjson::ImportPlan plan;
    const int other = tracks.addInstrumentTrackOfType("Synth");
    CHECK(chainjson::planImport(*tracks.project().findTrack(other), back, plan, error));
    CHECK(plan.synth && *plan.synth == sample());
    CHECK(plan.skipped.empty() && plan.addTypes.empty());

    const int harmonium = tracks.addInstrumentTrackOfType("Harmonium");
    CHECK(chainjson::planImport(*tracks.project().findTrack(harmonium), back, plan, error));
    CHECK(!plan.synth);
    CHECK((plan.skipped == std::vector<std::string>{std::string(effectchain::find("synth")->title)}));

    auto broken = back;
    broken.effects[0].settings["filter"]["cutoff"] = -1;
    CHECK(!chainjson::planImport(*tracks.project().findTrack(other), broken, plan, error));
    CHECK(!error.empty());
}
