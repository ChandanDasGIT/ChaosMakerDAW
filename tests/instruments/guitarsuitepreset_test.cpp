#include "testkit.h"
#include "instruments/guitar/presets/guitarsuitepreset.h"

TEST(guitarsuitepreset, parse_valid_preset_and_validate_schema) {
    const std::string json = R"({
        "format": "GuitarSuitePreset",
        "version": 1,
        "name": "CustomRock",
        "inputTrimDb": -4.5,
        "pedals": [
            {
                "type": "pedal.overdrive",
                "bypassed": false,
                "parameters": {
                    "drive": 0.8,
                    "tone": 0.5,
                    "level": 0.7
                }
            },
            {
                "type": "pedal.cabinet",
                "bypassed": true,
                "irPath": "cabs/my_cab.wav",
                "parameters": {
                    "level": -3.0
                }
            }
        ]
    })";

    std::string error;
    auto preset = GuitarSuitePreset::parse(json, "/tmp/presets", &error);
    CHECK(preset.has_value());
    CHECK(error.empty());
    CHECK(preset->name == "CustomRock");
    CHECK(preset->inputTrimDb == -4.5);
    CHECK(preset->pedals.size() == 2);
    CHECK(preset->pedals[0].type == "pedal.overdrive");
    CHECK(!preset->pedals[0].bypassed);
    CHECK(preset->pedals[0].parameters.at("drive") == 0.8f);
    CHECK(preset->pedals[1].type == "pedal.cabinet");
    CHECK(preset->pedals[1].bypassed);
    CHECK(preset->pedals[1].irPath.find("cabs/my_cab.wav") != std::string::npos);
}

TEST(guitarsuitepreset, reject_invalid_schema_or_out_of_range_trim) {
    std::string err;

    // Bad format name
    CHECK(!GuitarSuitePreset::parse(R"({"format": "Wrong", "version": 1, "pedals": []})", {}, &err).has_value());
    CHECK(err == "Invalid or unsupported GuitarSuite preset.");

    // Trim out of range [-24, 24]
    CHECK(!GuitarSuitePreset::parse(R"({"format": "GuitarSuitePreset", "version": 1, "inputTrimDb": 50, "pedals": []})", {}, &err).has_value());
    CHECK(err == "Invalid input trim in GuitarSuite preset.");

    // Not json
    CHECK(!GuitarSuitePreset::parse("invalid", {}, &err).has_value());
}

TEST(guitarsuitepreset, serialize_round_trip) {
    GuitarSuitePresetData data;
    data.name = "TestChain";
    data.inputTrimDb = 3.0;

    PedalPresetData p1;
    p1.type = "pedal.delay";
    p1.bypassed = false;
    p1.parameters["time"] = 350.0f;
    data.pedals.push_back(p1);

    std::string serialized = GuitarSuitePreset::serialize(data);
    auto restored = GuitarSuitePreset::parse(serialized);
    CHECK(restored.has_value());
    CHECK(restored->name == "TestChain");
    CHECK(restored->inputTrimDb == 3.0);
    CHECK(restored->pedals.size() == 1);
    CHECK(restored->pedals[0].type == "pedal.delay");
    CHECK(restored->pedals[0].parameters.at("time") == 350.0f);
}

TEST(guitarsuitepreset, instantiate_slots_and_cabinet_reuse) {
    GuitarSuitePresetData data;
    data.inputTrimDb = -2.0;

    PedalPresetData p1;
    p1.type = "pedal.overdrive";
    p1.parameters["drive"] = 0.6f;
    data.pedals.push_back(p1);

    int nextId = 1;
    std::vector<std::shared_ptr<PedalSlot>> existing;
    std::vector<std::shared_ptr<PedalSlot>> slots;
    std::string error;

    CHECK(GuitarSuitePreset::instantiateSlots(data, nextId, existing, 48000.0, false, slots, &error));
    CHECK(slots.size() == 1);
    CHECK(slots[0]->id == 1);
    CHECK(slots[0]->type == "pedal.overdrive");
    CHECK(nextId == 2);
}
