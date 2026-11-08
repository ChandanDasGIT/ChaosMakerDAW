#include "testkit.h"
#include "persistence/effectchainjson.h"
#include "plugins/effectchain.h"
#include "session/tracklist.h"

#include <algorithm>

using namespace persistence;
using nlohmann::json;

namespace {

std::string title(std::string_view type) { return std::string(effectchain::find(type)->title); }

std::vector<std::string> types(const chainjson::ChainFile& chain)
{
    std::vector<std::string> out;
    for (const auto& e : chain.effects) out.push_back(e.type);
    return out;
}

// A guitar track with GuitarSuite, then Chaos EQ and the 10-band EQ, with settings changed.
int guitarWithEffects(TrackList& tracks)
{
    const int g = tracks.addInstrumentTrackOfType("Electric Guitar");
    CHECK(tracks.addTrackEffect(g, "chaoseq") >= 0);
    CHECK(tracks.addTrackEffect(g, "eq") >= 0);
    CHECK(tracks.setEqBand(g, 2, 4.5).accepted);
    CHECK(tracks.setEqBand(g, 9, -6.0).accepted);
    auto chaos = tracks.project().findTrack(g)->chaosEq;
    chaos.outputGainDb = -3.0;
    CHECK(tracks.setChaosEq(g, chaos).accepted);
    return g;
}

} // namespace

TEST(effectchainjson, a_track_chain_is_captured_in_order_and_round_trips)
{
    TrackList tracks;
    const int g = guitarWithEffects(tracks);
    const json pedals = {{"format", "pedals"}, {"slots", json::array({1, 2})}};
    const auto chain = chainjson::capture(*tracks.project().findTrack(g), pedals);
    CHECK(chain.trackType == "Electric Guitar");
    CHECK((types(chain) == std::vector<std::string>{"guitarsuite", "chaoseq", "eq"}));
    CHECK(chain.effects[0].settings == pedals);
    CHECK(chain.effects[2].settings["gains"].size() == 10);
    CHECK(chain.effects[2].settings["gains"][2].get<double>() == 4.5);
    CHECK(chain.effects[1].settings["output"].get<double>() == -3.0);

    chainjson::ChainFile loaded;
    std::string error;
    CHECK(chainjson::fromJson(json::parse(chainjson::toJson(chain).dump()), loaded, error));
    CHECK(loaded.trackType == "Electric Guitar");
    CHECK(types(loaded) == types(chain));
    for (std::size_t i = 0; i < loaded.effects.size(); ++i) CHECK(loaded.effects[i].settings == chain.effects[i].settings);

    // Without a pedalboard the GuitarSuite entry is still there, with empty settings.
    const auto bare = chainjson::capture(*tracks.project().findTrack(g));
    CHECK(bare.effects[0].type == "guitarsuite" && bare.effects[0].settings.empty());
}

TEST(effectchainjson, files_that_are_not_chains_or_are_broken_are_refused)
{
    TrackList tracks;
    const auto good = chainjson::toJson(chainjson::capture(*tracks.project().findTrack(guitarWithEffects(tracks))));
    chainjson::ChainFile kept;
    kept.trackType = "unchanged";
    std::string error;
    const auto refused = [&](auto edit) {
        json j = good;
        edit(j);
        const bool ok = chainjson::fromJson(j, kept, error);
        return !ok && kept.trackType == "unchanged" && !error.empty();
    };
    CHECK(refused([](json& j) { j["format"] = "ChaosMakerGroove"; }));
    CHECK(refused([](json& j) { j["version"] = 2; }));
    CHECK(refused([](json& j) { j.erase("effects"); }));
    CHECK(refused([](json& j) { j["effects"][1]["type"] = "reverb9000"; }));
    CHECK(refused([](json& j) { j["effects"][2]["type"] = "chaoseq"; }));             // twice
    CHECK(refused([](json& j) { j["effects"][1]["settings"] = 5; }));
    CHECK(refused([](json& j) {
        j["effects"] = json::array();
        for (int i = 0; i < 17; ++i) j["effects"].push_back({{"type", "eq"}});
    }));
    CHECK(refused([](json& j) { j = json::array(); }));
    CHECK(chainjson::fromJson(good, kept, error) && error.empty());
}

TEST(effectchainjson, importing_replaces_the_chain_and_skips_what_the_track_cannot_take)
{
    TrackList tracks;
    const int g = guitarWithEffects(tracks);
    const auto guitarChain = chainjson::capture(*tracks.project().findTrack(g), json{{"pedals", 1}});

    // Onto a vocal: GuitarSuite can't come (a guitar's own page); the EQs do, in the file's order.
    const int vocal = tracks.addVocalTrack();
    chainjson::ImportPlan plan;
    std::string error;
    CHECK(chainjson::planImport(*tracks.project().findTrack(vocal), guitarChain, plan, error));
    CHECK((plan.skipped == std::vector<std::string>{title("guitarsuite")}));
    CHECK((plan.addTypes == std::vector<std::string>{"chaoseq", "eq"}));
    CHECK((plan.finalOrder == std::vector<std::string>{"chaoseq", "eq"}));
    CHECK(plan.kept.size() == 2);
    CHECK(plan.eqGainsDb && (*plan.eqGainsDb)[2] == 4.5f && (*plan.eqGainsDb)[9] == -6.0f);
    CHECK(plan.chaosEq && plan.chaosEq->outputGainDb == -3.0);
    CHECK(!plan.guitarSuite);

    // Onto another guitar: everything applies, the pedalboard too.
    const int other = tracks.addInstrumentTrackOfType("Acoustic Guitar");
    CHECK(chainjson::planImport(*tracks.project().findTrack(other), guitarChain, plan, error));
    CHECK(plan.skipped.empty());
    CHECK((plan.finalOrder == std::vector<std::string>{"guitarsuite", "chaoseq", "eq"}));
    CHECK((plan.addTypes == std::vector<std::string>{"chaoseq", "eq"}));
    CHECK(plan.guitarSuite && (*plan.guitarSuite)["pedals"] == 1);

    // A vocal chain (EQ only) onto a guitar: GuitarSuite stays first, the EQ replaces the rest.
    const int v2 = tracks.addVocalTrack();
    CHECK(tracks.addTrackEffect(v2, "eq") >= 0);
    const auto vocalChain = chainjson::capture(*tracks.project().findTrack(v2));
    CHECK(chainjson::planImport(*tracks.project().findTrack(g), vocalChain, plan, error));
    CHECK((plan.finalOrder == std::vector<std::string>{"guitarsuite", "eq"}));
    CHECK((plan.addTypes == std::vector<std::string>{"eq"}));
    CHECK(plan.skipped.empty() && !plan.chaosEq);
}

TEST(effectchainjson, instrument_pages_only_apply_to_the_same_instrument)
{
    TrackList tracks;
    const int drums = tracks.addInstrumentTrackOfType("Drums");
    auto kit = tracks.project().findTrack(drums)->drumkit;
    kit.kickDb = -4.0;
    CHECK(tracks.setDrumKit(drums, kit).accepted);
    const auto drumChain = chainjson::capture(*tracks.project().findTrack(drums));
    CHECK((types(drumChain) == std::vector<std::string>{"drumkit"}));

    chainjson::ImportPlan plan;
    std::string error;
    const int second = tracks.addInstrumentTrackOfType("Drums");
    CHECK(chainjson::planImport(*tracks.project().findTrack(second), drumChain, plan, error));
    CHECK(plan.drumkit && plan.drumkit->kickDb == -4.0);
    CHECK(plan.skipped.empty() && plan.addTypes.empty());

    const int electro = tracks.addInstrumentTrackOfType("Electronic Drums");
    CHECK(chainjson::planImport(*tracks.project().findTrack(electro), drumChain, plan, error));
    CHECK(!plan.drumkit);
    CHECK((plan.skipped == std::vector<std::string>{title("drumkit")}));
    CHECK((plan.finalOrder == std::vector<std::string>{"electrodrums"}));       // its own page stays

    // Settings that do not read: nothing planned.
    auto broken = drumChain;
    broken.effects[0].settings["kick"] = 999;
    plan.kept = {"untouched"};
    CHECK(!chainjson::planImport(*tracks.project().findTrack(second), broken, plan, error));
    CHECK(!error.empty() && plan.kept == std::vector<std::string>{"untouched"});
    chainjson::ChainFile badEq;
    badEq.effects.push_back({"eq", {{"gains", json::array({99, 0, 0, 0, 0, 0, 0, 0, 0, 0})}}});
    CHECK(!chainjson::planImport(*tracks.project().findTrack(second), badEq, plan, error));
}
