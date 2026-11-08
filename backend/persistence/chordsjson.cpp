#include "chordsjson.h"

namespace persistence::chordsjson {

using nlohmann::json;

json toJson(const chords::Settings& s)
{
    json names = json::array();
    for (const auto& n : s.chords) names.push_back(n);
    return {{"chords", std::move(names)}, {"autoplay", s.autoplay}, {"key", s.key.tonic}, {"minor", s.key.minor}};
}

bool fromJson(const json& j, chords::Settings& settings, std::string& error)
{
    error = "Invalid Chords settings.";
    if (!j.is_object()) return false;
    chords::Settings r;
    if (j.contains("chords")) {
        const auto& names = j["chords"];
        if (!names.is_array() || names.size() != r.chords.size()) return false;
        for (std::size_t i = 0; i < r.chords.size(); ++i) {
            if (!names[i].is_string()) return false;
            const auto text = names[i].get<std::string>();
            if (text.size() > 32) return false;
            if (!text.empty() && !chords::parse(text)) return false;
            r.chords[i] = text;
        }
    }
    if (j.contains("autoplay")) {
        if (!j["autoplay"].is_number_integer()) return false;
        r.autoplay = j["autoplay"].get<int>();
        if (r.autoplay < 0 || r.autoplay >= chords::AutoplayModes) return false;
    }
    if (j.contains("key")) {
        if (!j["key"].is_number_integer()) return false;
        r.key.tonic = j["key"].get<int>();
        if (r.key.tonic < 0 || r.key.tonic > 11) return false;
    }
    if (j.contains("minor")) {
        if (!j["minor"].is_boolean()) return false;
        r.key.minor = j["minor"].get<bool>();
    }
    settings = chords::sanitize(r);
    error.clear();
    return true;
}

} // namespace persistence::chordsjson
