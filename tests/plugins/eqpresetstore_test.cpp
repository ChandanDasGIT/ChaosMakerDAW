#include "testkit.h"
#include "plugins/eq/eqpresetstore.h"
#include "plugins/eq/tenbandeq.h"

#include <cstdio>
#include <filesystem>

TEST(eqpresetstore, factory_presets_count_and_names) {
    const auto names = EqPresetStore::factoryPresetNames();
    CHECK(names.size() == 7);
    CHECK(names[0] == "Flat");
    CHECK(names[1] == "Electric Guitar");
    CHECK(names[6] == "Bass Cut");

    auto flat = EqPresetStore::factoryPresetGains(0);
    CHECK(flat.has_value());
    for (float g : *flat) {
        CHECK(g == 0.0f);
    }

    auto guitar = EqPresetStore::factoryPresetGains("Electric Guitar");
    CHECK(guitar.has_value());
    CHECK((*guitar)[0] == -6.0f);
    CHECK((*guitar)[4] == 2.0f);

    CHECK(!EqPresetStore::factoryPresetGains(-1).has_value());
    CHECK(!EqPresetStore::factoryPresetGains(99).has_value());
    CHECK(!EqPresetStore::factoryPresetGains("Nonexistent").has_value());
}

TEST(eqpresetstore, parse_and_serialize_round_trip) {
    std::array<float, 10> original = {-3.5f, 0.0f, 1.5f, -2.0f, 4.0f, 2.5f, -1.0f, 0.5f, 3.0f, -6.0f};
    std::string json = EqPresetStore::serialize(original);
    CHECK(!json.empty());

    std::string error;
    auto parsed = EqPresetStore::parse(json, &error);
    CHECK(parsed.has_value());
    CHECK(error.empty());
    CHECK(parsed->format == "ChaosMakerEQ");
    CHECK(parsed->version == 1);
    CHECK(parsed->gainsDb == original);
    CHECK(parsed->frequenciesHz == TenBandEq::frequencies());
}

TEST(eqpresetstore, reject_invalid_schema_and_out_of_range_values) {
    std::string error;

    // Bad format
    const char* badFormat = R"({"format":"OtherEQ","version":1,"frequenciesHz":[31.25,62.5,125,250,500,1000,2000,4000,8000,16000],"gainsDb":[0,0,0,0,0,0,0,0,0,0]})";
    CHECK(!EqPresetStore::parse(badFormat, &error).has_value());
    CHECK(error == "Invalid or unsupported EQ preset.");

    // Gain out of range (> 12 dB)
    const char* badGain = R"({"format":"ChaosMakerEQ","version":1,"frequenciesHz":[31.25,62.5,125,250,500,1000,2000,4000,8000,16000],"gainsDb":[15,0,0,0,0,0,0,0,0,0]})";
    CHECK(!EqPresetStore::parse(badGain, &error).has_value());
    CHECK(error == "EQ preset must contain ten matching bands within ±12 dB.");

    // Wrong frequency
    const char* badHz = R"({"format":"ChaosMakerEQ","version":1,"frequenciesHz":[32,62.5,125,250,500,1000,2000,4000,8000,16000],"gainsDb":[0,0,0,0,0,0,0,0,0,0]})";
    CHECK(!EqPresetStore::parse(badHz, &error).has_value());
    CHECK(error == "EQ preset must contain ten matching bands within ±12 dB.");
}

TEST(eqpresetstore, file_save_and_load) {
    namespace fs = std::filesystem;
    const std::string tmpPath = (fs::temp_directory_path() / "test_preset.eqpreset").string();

    std::array<float, 10> sampleGains = {-1.0f, 2.0f, -3.0f, 4.0f, -5.0f, 6.0f, -7.0f, 8.0f, -9.0f, 10.0f};
    std::string error;
    CHECK(EqPresetStore::saveToFile(tmpPath, sampleGains, &error));

    std::array<float, 10> loadedGains{};
    CHECK(EqPresetStore::loadFromFile(tmpPath, loadedGains, &error));
    CHECK(loadedGains == sampleGains);

    std::remove(tmpPath.c_str());
}
