#include "testkit.h"
#include "instruments/guitar/plugins/guitarsuite/pedalregistry.h"

TEST(pedalregistry, available_pedals_contains_all_eighteen_effects) {
    const auto pedals = PedalRegistry::availablePedals();
    CHECK(pedals.size() == 18);

    const std::vector<std::string> expected = {
        "pedal.noisegate", "pedal.overdrive", "pedal.crunch", "pedal.distortion",
        "pedal.delay", "pedal.compressor", "pedal.wah", "pedal.chorus",
        "pedal.tremolo", "pedal.tuner", "pedal.reverb", "pedal.amp",
        "pedal.cabinet", "pedal.octaver", "pedal.pitchshift", "pedal.parametriceq",
        "pedal.fuzz", "pedal.phaser"
    };

    for (const auto& type : expected) {
        CHECK(std::find(pedals.begin(), pedals.end(), type) != pedals.end());
    }
}

TEST(pedalregistry, display_names_and_creation_by_id_and_alias) {
    CHECK(PedalRegistry::displayName("pedal.overdrive") == "Overdrive");
    CHECK(PedalRegistry::displayName("pedal.delay") == "Delay");
    CHECK(PedalRegistry::displayName("pedal.cabinet") == "Cabinet IR");
    CHECK(PedalRegistry::displayName("pedal.tuner") == "Tuner");
    CHECK(PedalRegistry::displayName("pedal.amp") == "Amp");
    CHECK(PedalRegistry::displayName("unknown") == "unknown");

    auto byId = PedalRegistry::create("pedal.distortion");
    CHECK(byId != nullptr);
    CHECK(byId->typeId() == "pedal.distortion");

    auto byName = PedalRegistry::create("Distortion");
    CHECK(byName != nullptr);
    CHECK(byName->typeId() == "pedal.distortion");

    auto amp = PedalRegistry::create("Amp");
    CHECK(amp != nullptr);
    CHECK(amp->typeId() == "pedal.amp");

    auto unknown = PedalRegistry::create("pedal.nonexistent");
    CHECK(unknown == nullptr);
}
