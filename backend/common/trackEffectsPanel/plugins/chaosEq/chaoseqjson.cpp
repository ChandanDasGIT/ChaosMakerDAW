#include "chaoseqjson.h"

#include <cmath>

namespace persistence::chaoseqjson {
namespace {

bool number(const nlohmann::json& object, const char* key, double min, double max, double& out)
{
    if (!object.contains(key) || !object[key].is_number()) return false;
    out = object[key].get<double>();
    return std::isfinite(out) && out >= min && out <= max;
}

bool flag(const nlohmann::json& object, const char* key, bool& out)
{
    if (!object.contains(key)) { out = false; return true; }   // optional, default off
    if (!object[key].is_boolean()) return false;
    out = object[key].get<bool>();
    return true;
}

} // namespace

nlohmann::json toJson(const chaoseq::Settings& settings)
{
    nlohmann::json bands = nlohmann::json::array();
    for (int slot = 0; slot < chaoseq::MaxBands; ++slot) {
        const auto& band = settings.bands[static_cast<std::size_t>(slot)];
        if (!band.inUse) continue;
        bands.push_back({{"slot", slot},
                         {"shape", std::string(chaoseq::shapeName(band.filter.shape))},
                         {"frequency", band.filter.frequencyHz},
                         {"gain", band.filter.gainDb},
                         {"q", band.filter.q},
                         {"slope", eqband::dbPerOctave(band.filter.slope)},
                         {"placement", std::string(chaoseq::placementName(band.placement))},
                         {"bypass", band.bypassed},
                         {"solo", band.solo}});
    }
    return {{"output", settings.outputGainDb}, {"autoGain", settings.autoGain}, {"bands", std::move(bands)}};
}

bool fromJson(const nlohmann::json& json, chaoseq::Settings& settings, std::string& error)
{
    error = "Invalid Chaos EQ settings.";
    if (!json.is_object() || !json.contains("bands") || !json["bands"].is_array()) return false;
    chaoseq::Settings result;
    if (!number(json, "output", chaoseq::MinOutputDb, chaoseq::MaxOutputDb, result.outputGainDb)) return false;
    if (!flag(json, "autoGain", result.autoGain)) return false;
    if (json["bands"].size() > std::size_t(chaoseq::MaxBands)) return false;

    for (const auto& item : json["bands"]) {
        if (!item.is_object() || !item.contains("slot") || !item["slot"].is_number_integer()) return false;
        const int slot = item["slot"].get<int>();
        if (slot < 0 || slot >= chaoseq::MaxBands) return false;
        auto& band = result.bands[static_cast<std::size_t>(slot)];
        if (band.inUse) return false;   // the same slot twice
        band.inUse = true;

        if (!item.contains("shape") || !item["shape"].is_string()
            || !chaoseq::shapeFromName(item["shape"].get<std::string>(), band.filter.shape))
            return false;
        if (!item.contains("placement") || !item["placement"].is_string()
            || !chaoseq::placementFromName(item["placement"].get<std::string>(), band.placement))
            return false;
        if (!item.contains("slope") || !item["slope"].is_number_integer()
            || !chaoseq::slopeFromDbPerOctave(item["slope"].get<int>(), band.filter.slope))
            return false;
        if (!number(item, "frequency", eqband::MinFrequencyHz, eqband::MaxFrequencyHz, band.filter.frequencyHz)
            || !number(item, "gain", eqband::MinGainDb, eqband::MaxGainDb, band.filter.gainDb)
            || !number(item, "q", eqband::MinQ, eqband::MaxQ, band.filter.q)
            || !flag(item, "bypass", band.bypassed) || !flag(item, "solo", band.solo))
            return false;
    }
    settings = result;
    error.clear();
    return true;
}

} // namespace persistence::chaoseqjson
