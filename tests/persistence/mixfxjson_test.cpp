#include "testkit.h"
#include "persistence/mixfxjson.h"

using nlohmann::json;

TEST(mixfxjson, settings_round_trip_and_bad_json_is_refused)
{
    auto settings = mixfx::defaults(mixfx::Compressor);
    settings.bypassed = true;
    settings.values["threshold"] = -30;
    mixfx::Settings back;
    std::string error;
    CHECK(mixfxjson::fromJson(mixfx::Compressor, mixfxjson::toJson(settings), back, error));
    CHECK(back == settings);

    // Missing values are defaults, unknown ones ignored, out of range clamped.
    CHECK(mixfxjson::fromJson(mixfx::Compressor, json::parse(R"({"values":{"ratio":99,"future":1}})"), back, error));
    CHECK(back.values.at("ratio") == 20.0f && back.values.count("future") == 0 && !back.bypassed);
    CHECK(back.values.at("threshold") == -18.0f);

    const auto before = back;
    for (const char* bad : {R"([])", R"({"bypassed":1})", R"({"values":[]})", R"({"values":{"ratio":"4"}})"}) {
        CHECK(!mixfxjson::fromJson(mixfx::Compressor, json::parse(bad), back, error));
        CHECK(!error.empty());
        CHECK(back == before);
    }
    CHECK(!mixfxjson::fromJson("nothing", json::object(), back, error));
}
