#include "harmoniumjson.h"

#include <cmath>

namespace persistence::harmoniumjson {
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

bool flag(const json& object, const char* key, bool& out)
{
    if (!object.contains(key)) return true;
    if (!object[key].is_boolean()) return false;
    out = object[key].get<bool>();
    return true;
}

// Optional sub-object; absent is fine, anything else must be an object.
const json* group(const json& object, const char* key, bool& ok)
{
    if (!object.contains(key)) return nullptr;
    if (!object[key].is_object()) { ok = false; return nullptr; }
    return &object[key];
}

} // namespace

json toJson(const harmonium::Settings& s)
{
    return {{"style", std::string(harmonium::styleName(s.style))},
            {"indian", {{"bass", s.indian.bass}, {"male", s.indian.male}, {"female", s.indian.female}}},
            {"western", {{"bourdon16", s.western.bourdon16}, {"diapason8", s.western.diapason8},
                         {"dulciana8", s.western.dulciana8}, {"principal4", s.western.principal4},
                         {"voxHumana", s.western.voxHumana}}},
            {"bellows", s.bellows}, {"pumping", s.pumping}, {"beating", s.beatingCents},
            {"airNoise", s.airNoise}, {"keyClick", s.keyClick}, {"case", s.caseResonance},
            {"transpose", s.transpose}, {"volume", s.volumeDb},
            {"drones", json(s.drones)}};
}

bool fromJson(const json& j, harmonium::Settings& settings, std::string& error)
{
    error = "Invalid harmonium settings.";
    if (!j.is_object()) return false;
    harmonium::Settings r;

    if (j.contains("style")) {
        if (!j["style"].is_string() || !harmonium::styleFromName(j["style"].get<std::string>(), r.style)) return false;
    }
    bool ok = true;
    if (const auto* g = group(j, "indian", ok)) {
        ok = flag(*g, "bass", r.indian.bass) && flag(*g, "male", r.indian.male) && flag(*g, "female", r.indian.female);
    }
    if (!ok) return false;
    if (const auto* g = group(j, "western", ok)) {
        ok = flag(*g, "bourdon16", r.western.bourdon16) && flag(*g, "diapason8", r.western.diapason8)
          && flag(*g, "dulciana8", r.western.dulciana8) && flag(*g, "principal4", r.western.principal4)
          && flag(*g, "voxHumana", r.western.voxHumana);
    }
    if (!ok) return false;

    if (!number(j, "bellows", 0.0, 1.0, r.bellows) || !number(j, "pumping", 0.0, 1.0, r.pumping)
        || !number(j, "beating", 0.0, harmonium::MaxBeatingCents, r.beatingCents)
        || !number(j, "airNoise", 0.0, 1.0, r.airNoise) || !number(j, "keyClick", 0.0, 1.0, r.keyClick)
        || !number(j, "case", 0.0, 1.0, r.caseResonance)
        || !number(j, "volume", harmonium::MinVolumeDb, harmonium::MaxVolumeDb, r.volumeDb))
        return false;

    if (j.contains("transpose")) {
        if (!j["transpose"].is_number_integer()) return false;
        const int t = j["transpose"].get<int>();
        if (t < -harmonium::MaxTranspose || t > harmonium::MaxTranspose) return false;
        r.transpose = t;
    }
    if (j.contains("drones")) {
        const auto& d = j["drones"];
        if (!d.is_array() || d.size() != std::size_t(harmonium::DroneCount)) return false;
        for (std::size_t i = 0; i < d.size(); ++i) {
            if (!d[i].is_number_integer()) return false;
            const int key = d[i].get<int>();
            if (key != harmonium::NoDrone && (key < 0 || key > 127)) return false;
            r.drones[i] = key;
        }
    }
    settings = r;
    error.clear();
    return true;
}

} // namespace persistence::harmoniumjson
