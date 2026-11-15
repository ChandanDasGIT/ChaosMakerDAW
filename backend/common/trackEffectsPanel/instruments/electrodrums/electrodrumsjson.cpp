#include "electrodrumsjson.h"

#include <cmath>

namespace persistence::electrodrumsjson {
namespace {

using nlohmann::json;

// Optional number: absent keeps `out`; present must be finite and in range.
bool number(const json& object, const char* key, double min, double max, double& out)
{
    if (!object.contains(key)) return true;
    if (!object[key].is_number()) return false;
    const double value = object[key].get<double>();
    if (!std::isfinite(value) || value < min || value > max) return false;
    out = value;
    return true;
}

} // namespace

json toJson(const electrodrums::Settings& s)
{
    json drums = json::object();
    for (int i = 0; i < electrodrums::DrumCount; ++i) {
        const auto& d = s.drums[std::size_t(i)];
        drums[std::string(electrodrums::drumId(electrodrums::Drum(i)))] =
            {{"tune", d.tuneSemitones}, {"decay", d.decay}, {"level", d.levelDb}};
    }
    return {{"style", std::string(electrodrums::styleId(s.style))}, {"volume", s.volumeDb},
            {"drums", std::move(drums)}};
}

bool fromJson(const json& j, electrodrums::Settings& settings, std::string& error)
{
    using namespace electrodrums;
    error = "Invalid electronic drum settings.";
    if (!j.is_object()) return false;
    Settings r;
    if (j.contains("style")) {
        if (!j["style"].is_string() || !styleFromId(j["style"].get<std::string>(), r.style)) return false;
    }
    if (!number(j, "volume", MinLevelDb, MaxLevelDb, r.volumeDb)) return false;
    if (j.contains("drums")) {
        const auto& drums = j["drums"];
        if (!drums.is_object()) return false;
        for (const auto& [name, value] : drums.items()) {
            Drum drum;
            if (!drumFromId(name, drum) || !value.is_object()) return false;
            auto& d = r.drum(drum);
            if (!number(value, "tune", MinTune, MaxTune, d.tuneSemitones)
                || !number(value, "decay", MinDecay, MaxDecay, d.decay)
                || !number(value, "level", MinLevelDb, MaxLevelDb, d.levelDb))
                return false;
        }
    }
    settings = r;
    error.clear();
    return true;
}

} // namespace persistence::electrodrumsjson
