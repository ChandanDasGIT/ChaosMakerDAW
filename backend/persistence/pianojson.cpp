#include "pianojson.h"

#include <cmath>

namespace persistence::pianojson {
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

json toJson(const piano::Settings& s)
{
    return {{"volume", s.volumeDb}, {"release", s.release}, {"resonance", s.resonance},
            {"velocity", s.velocity}, {"brightness", s.brightness}, {"transpose", s.transpose}};
}

bool fromJson(const json& j, piano::Settings& settings, std::string& error)
{
    error = "Invalid piano settings.";
    if (!j.is_object()) return false;
    piano::Settings r;
    if (!number(j, "volume", piano::MinVolumeDb, piano::MaxVolumeDb, r.volumeDb)
        || !number(j, "release", 0.0, 1.0, r.release) || !number(j, "resonance", 0.0, 1.0, r.resonance)
        || !number(j, "velocity", 0.0, 1.0, r.velocity) || !number(j, "brightness", 0.0, 1.0, r.brightness))
        return false;
    if (j.contains("transpose")) {
        if (!j["transpose"].is_number_integer()) return false;
        const auto t = j["transpose"].get<long long>();
        if (t < -piano::MaxTranspose || t > piano::MaxTranspose) return false;
        r.transpose = int(t);
    }
    settings = r;
    error.clear();
    return true;
}

} // namespace persistence::pianojson
