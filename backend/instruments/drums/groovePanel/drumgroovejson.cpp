#include "drumgroovejson.h"

#include <algorithm>
#include <cmath>

namespace persistence::drumgroovejson {

using nlohmann::json;

json toJson(const drumgroove::Groove& g)
{
    json notes = json::array();
    for (const auto& n : g.notes)
        notes.push_back({{"key", n.note}, {"beat", n.startBeat}, {"length", n.lengthBeats}, {"velocity", n.velocity}});
    return {{"format", "ChaosMakerGroove"}, {"version", 1}, {"name", g.name},
            {"meter", json::array({g.numerator, g.denominator})}, {"bars", g.bars}, {"notes", std::move(notes)}};
}

bool fromJson(const json& j, drumgroove::Groove& groove, std::string& error)
{
    error = "Not a valid groove file.";
    if (!j.is_object() || j.value("format", std::string()) != "ChaosMakerGroove") return false;
    if (!j.contains("version") || !j["version"].is_number_integer() || j["version"].get<int>() != 1) {
        error = "This groove file was made by a newer version.";
        return false;
    }
    drumgroove::Groove g;
    if (!j.contains("name") || !j["name"].is_string()) return false;
    const auto name = drumgroove::cleanName(j["name"].get<std::string>());
    if (!name) return false;
    g.name = *name;
    const auto& meter = j.value("meter", json());
    if (!meter.is_array() || meter.size() != 2 || !meter[0].is_number_integer() || !meter[1].is_number_integer()) return false;
    g.numerator = meter[0].get<int>();
    g.denominator = meter[1].get<int>();
    if (!drumgroove::validMeter(g.numerator, g.denominator)) return false;
    if (!j.contains("bars") || !j["bars"].is_number_integer()) return false;
    g.bars = j["bars"].get<int>();
    if (g.bars < 1 || g.bars > drumgroove::MaxBars) return false;
    if (!j.contains("notes") || !j["notes"].is_array() || j["notes"].empty()
        || j["notes"].size() > drumgroove::MaxNotes) return false;

    const double end = g.lengthBeats();
    int id = 1;
    for (const auto& item : j["notes"]) {
        if (!item.is_object()) return false;
        const auto number = [&](const char* key) -> const json* {
            return item.contains(key) && item[key].is_number() ? &item[key] : nullptr;
        };
        const json* key = number("key"); const json* beat = number("beat");
        const json* length = number("length"); const json* velocity = number("velocity");
        if (!key || !beat || !length || !velocity || !key->is_number_integer() || !velocity->is_number_integer()) return false;
        MidiNote n;
        n.id = id++;
        n.note = key->get<int>();
        n.startBeat = beat->get<double>();
        n.lengthBeats = length->get<double>();
        n.velocity = velocity->get<int>();
        if (n.note < 0 || n.note > 127 || n.velocity < 1 || n.velocity > 127) return false;
        if (!std::isfinite(n.startBeat) || !std::isfinite(n.lengthBeats) || n.startBeat < 0
            || n.startBeat >= end || n.lengthBeats <= 0 || n.lengthBeats > end) return false;
        g.notes.push_back(n);
    }
    std::stable_sort(g.notes.begin(), g.notes.end(), [](const MidiNote& a, const MidiNote& b) { return a.startBeat < b.startBeat; });
    g.factory = false;
    groove = std::move(g);
    error.clear();
    return true;
}

} // namespace persistence::drumgroovejson
