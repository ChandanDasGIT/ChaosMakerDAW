#include "persistence/mixfxjson.h"

using json = nlohmann::json;

namespace mixfxjson {

json toJson(const mixfx::Settings& settings)
{
    json values = json::object();
    for (const auto& [id, v] : settings.values) values[id] = v;
    json out{{"bypassed", settings.bypassed}, {"values", std::move(values)}};
    if (!settings.profile.empty()) out["profile"] = settings.profile;   // noise reduction (G13)
    return out;
}

bool fromJson(std::string_view type, const json& j, mixfx::Settings& settings, std::string& error)
{
    const auto* spec = mixfx::find(type);
    const auto fail = [&](const char* what) { error = what; return false; };
    if (!spec) return fail("Unknown mixing effect.");
    if (!j.is_object()) return fail("Mixing effect settings must be an object.");
    mixfx::Settings read;
    if (j.contains("bypassed")) {
        if (!j["bypassed"].is_boolean()) return fail("Mixing effect bypass must be true or false.");
        read.bypassed = j["bypassed"].get<bool>();
    }
    if (j.contains("values")) {
        const auto& values = j["values"];
        if (!values.is_object()) return fail("Mixing effect values must be an object.");
        for (const auto& [id, v] : values.items()) {
            if (!v.is_number()) return fail("Mixing effect values must be numbers.");
            read.values[id] = v.get<float>();
        }
    }
    if (j.contains("profile")) {   // noise reduction (restMap G13); optional
        const auto& profile = j["profile"];
        if (!profile.is_array()) return fail("A noise profile must be a list of numbers.");
        for (const auto& v : profile) {
            if (!v.is_number()) return fail("A noise profile must be a list of numbers.");
            read.profile.push_back(v.get<float>());
        }
    }
    settings = mixfx::sanitize(type, read);
    error.clear();
    return true;
}

} // namespace mixfxjson
