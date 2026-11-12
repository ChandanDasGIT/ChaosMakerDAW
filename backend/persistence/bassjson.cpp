#include "bassjson.h"

#include <cmath>

namespace persistence::bassjson {
namespace {

using nlohmann::json;
using namespace bass;

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

bool integer(const json& object, const char* key, int min, int max, int& out)
{
    if (!object.contains(key)) return true;
    if (!object[key].is_number_integer()) return false;
    const auto value = object[key].get<long long>();
    if (value < min || value > max) return false;
    out = int(value);
    return true;
}

bool boolean(const json& object, const char* key, bool& out)
{
    if (!object.contains(key)) return true;
    if (!object[key].is_boolean()) return false;
    out = object[key].get<bool>();
    return true;
}

bool style(const json& object, const char* key, Style& out)
{
    if (!object.contains(key)) return true;
    return object[key].is_string() && styleFromName(object[key].get<std::string>(), out);
}

} // namespace

json toJson(const Settings& s)
{
    return {
        {"style", std::string(styleName(s.style))},
        {"muteAmount", s.muteAmount},
        {"slapOnHardHits", s.slapOnHardHits},
        {"slapVelocity", s.slapVelocity},
        {"keyswitches", s.keyswitches},
        {"pluckPosition", s.pluckPosition},
        {"sustain", s.sustain},
        {"brightness", s.brightness},
        {"velocity", s.velocity},
        {"pickupBlend", s.pickupBlend},
        {"tone", s.tone},
        {"drive", s.drive},
        {"compression", s.compression},
        {"slide", s.slideSeconds},
        {"bendRange", s.bendSemitones},
        {"volume", s.volumeDb},
    };
}

bool fromJson(const json& j, Settings& settings, std::string& error)
{
    error = "Invalid bass settings.";
    if (!j.is_object()) return false;
    Settings r;
    if (!style(j, "style", r.style)
        || !number(j, "muteAmount", 0.0, 1.0, r.muteAmount)
        || !boolean(j, "slapOnHardHits", r.slapOnHardHits)
        || !integer(j, "slapVelocity", 1, 127, r.slapVelocity)
        || !boolean(j, "keyswitches", r.keyswitches)
        || !number(j, "pluckPosition", 0.0, 1.0, r.pluckPosition)
        || !number(j, "sustain", 0.0, 1.0, r.sustain)
        || !number(j, "brightness", 0.0, 1.0, r.brightness)
        || !number(j, "velocity", 0.0, 1.0, r.velocity)
        || !number(j, "pickupBlend", 0.0, 1.0, r.pickupBlend)
        || !number(j, "tone", 0.0, 1.0, r.tone)
        || !number(j, "drive", 0.0, 1.0, r.drive)
        || !number(j, "compression", 0.0, 1.0, r.compression)
        || !number(j, "slide", 0.0, MaxSlideSeconds, r.slideSeconds)
        || !integer(j, "bendRange", 0, MaxBendSemitones, r.bendSemitones)
        || !number(j, "volume", MinVolumeDb, MaxVolumeDb, r.volumeDb))
        return false;
    settings = r;
    error.clear();
    return true;
}

json presetToJson(const Preset& preset)
{
    return {{"format", "ChaosMakerBassPreset"}, {"version", 1}, {"name", preset.name},
            {"settings", toJson(preset.settings)}};
}

bool presetFromJson(const json& j, Preset& preset, std::string& error)
{
    error = "This is not a bass preset file.";
    if (!j.is_object() || j.value("format", std::string()) != "ChaosMakerBassPreset") return false;
    if (!j.contains("version") || !j["version"].is_number_integer() || j["version"].get<int>() != 1) {
        error = "This bass preset was made by a newer version.";
        return false;
    }
    if (!j.contains("name") || !j["name"].is_string()) { error = "The bass preset has no name."; return false; }
    const auto name = cleanPresetName(j["name"].get<std::string>());
    if (!name) { error = "The bass preset's name is not usable."; return false; }
    Settings settings;
    if (!j.contains("settings") || !fromJson(j["settings"], settings, error)) {
        error = "The bass preset's settings do not read.";
        return false;
    }
    preset = {*name, settings, false};
    error.clear();
    return true;
}

} // namespace persistence::bassjson
