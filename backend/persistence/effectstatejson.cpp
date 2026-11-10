#include "persistence/effectstatejson.h"
#include "persistence/chaoseqjson.h"
#include "persistence/mixfxjson.h"
#include "session/project.h"

#include <cmath>

using json = nlohmann::json;

namespace effectstatejson {

json toJson(const EffectState& state)
{
    json result = json::object();
    json list = json::array();
    for (const auto& slot : state.effects) list.push_back(slot.type);
    result["effects"] = std::move(list);
    json eq = json::array();
    for (float g : state.eqGainDb) eq.push_back(g);
    result["eq"] = std::move(eq);
    result["chaosEq"] = persistence::chaoseqjson::toJson(state.chaosEq);
    json mix = json::object();
    for (const auto& [type, settings] : state.mixEffects) mix[type] = mixfxjson::toJson(settings);
    result["mixEffects"] = std::move(mix);
    return result;
}

bool apply(const json& j, Project& project, int ownerId, std::string& error)
{
    const auto bad = [&error](const char* what) { error = what; return false; };
    if (!j.is_object() || !project.effects(ownerId)) return bad("Invalid effects.");
    if (j.contains("effects")) {
        std::vector<std::string> types;
        if (!j["effects"].is_array()) return bad("Invalid effects.");
        for (const auto& e : j["effects"]) {
            if (!e.is_string()) return bad("Invalid effects.");
            types.push_back(e.get<std::string>());
        }
        if (!project.setTrackEffects(ownerId, types)) return bad("Invalid effects.");
    }
    if (j.contains("eq")) {
        const auto& eq = j["eq"];
        if (!eq.is_array() || eq.size() != 10) return bad("Invalid EQ.");
        for (int i = 0; i < 10; ++i) {
            if (!eq[i].is_number()) return bad("Invalid EQ.");
            const double gain = eq[i].get<double>();
            if (!std::isfinite(gain) || gain < -12.0 || gain > 12.0) return bad("Invalid EQ.");
            project.setTrackEqBand(ownerId, i, static_cast<float>(gain));
        }
    }
    if (j.contains("chaosEq")) {
        chaoseq::Settings chaos;
        if (!persistence::chaoseqjson::fromJson(j["chaosEq"], chaos, error)) return false;
        project.setTrackChaosEq(ownerId, chaos);
    }
    if (j.contains("mixEffects")) {
        if (!j["mixEffects"].is_object()) return bad("Invalid effects.");
        for (const auto& [type, settingsJson] : j["mixEffects"].items()) {
            mixfx::Settings settings;
            if (!mixfxjson::fromJson(type, settingsJson, settings, error)) return false;
            project.setTrackMixEffect(ownerId, type, settings);
        }
    }
    return true;
}

} // namespace effectstatejson
