#include "effectchainjson.h"

#include "chaoseqjson.h"
#include "chordsjson.h"
#include "drumkitjson.h"
#include "electrodrumsjson.h"
#include "harmoniumjson.h"
#include "synthjson.h"
#include "bassjson.h"
#include "mixfxjson.h"
#include "pianojson.h"
#include "plugins/effectchain.h"
#include "plugins/eq/tenbandeq.h"
#include "session/project.h"

#include <algorithm>
#include <cmath>

namespace persistence::chainjson {

using nlohmann::json;

namespace {

std::string titleOf(const std::string& type)
{
    const auto* def = effectchain::find(type);
    return def ? std::string(def->title) : type;
}

bool contains(const std::vector<std::string>& list, const std::string& value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

} // namespace

json toJson(const ChainFile& chain)
{
    json effects = json::array();
    for (const auto& e : chain.effects) effects.push_back({{"type", e.type}, {"settings", e.settings}});
    return {{"format", "ChaosMakerChain"}, {"version", 1}, {"trackType", chain.trackType}, {"effects", std::move(effects)}};
}

bool fromJson(const json& j, ChainFile& chain, std::string& error)
{
    error = "Not an FX chain file.";
    if (!j.is_object() || j.value("format", std::string()) != "ChaosMakerChain") return false;
    if (!j.contains("version") || !j["version"].is_number_integer()) return false;
    if (j["version"].get<int>() != 1) { error = "This FX chain was saved by a newer version."; return false; }
    if (!j.contains("effects") || !j["effects"].is_array()) return false;
    if (j["effects"].size() > MaxEffects) { error = "The FX chain has too many effects."; return false; }

    ChainFile result;
    if (j.contains("trackType")) {
        if (!j["trackType"].is_string()) return false;
        result.trackType = j["trackType"].get<std::string>().substr(0, 64);
    }
    std::vector<std::string> seen;
    for (const auto& item : j["effects"]) {
        if (!item.is_object() || !item.contains("type") || !item["type"].is_string()) return false;
        Effect e;
        e.type = item["type"].get<std::string>();
        if (!effectchain::find(e.type)) { error = "The FX chain has an effect this version doesn't know: " + e.type.substr(0, 40) + "."; return false; }
        if (contains(seen, e.type)) { error = "The FX chain lists " + titleOf(e.type) + " twice."; return false; }
        seen.push_back(e.type);
        if (item.contains("settings")) {
            if (!item["settings"].is_object()) { error = "The " + titleOf(e.type) + " settings in the FX chain do not read."; return false; }
            e.settings = item["settings"];
        }
        result.effects.push_back(std::move(e));
    }
    chain = std::move(result);
    error.clear();
    return true;
}

ChainFile capture(const Track& track, const json& guitarSuite)
{
    ChainFile chain;
    chain.trackType = track.type;
    for (const auto& slot : track.effects) {
        Effect e;
        e.type = slot.type;
        if (slot.type == effectchain::Eq) {
            json gains = json::array();
            for (float g : track.eqGainDb) gains.push_back(g);
            e.settings = {{"gains", std::move(gains)}};
        } else if (slot.type == effectchain::ChaosEq) {
            e.settings = chaoseqjson::toJson(track.chaosEq);
        } else if (slot.type == effectchain::Harmonium) {
            e.settings = harmoniumjson::toJson(track.harmonium);
        } else if (slot.type == effectchain::DrumKit) {
            e.settings = drumkitjson::toJson(track.drumkit);
        } else if (slot.type == effectchain::ElectroDrums) {
            e.settings = electrodrumsjson::toJson(track.electrodrums);
        } else if (slot.type == effectchain::Piano) {
            e.settings = pianojson::toJson(track.piano);
        } else if (slot.type == effectchain::Chords) {
            e.settings = chordsjson::toJson(track.chords);
        } else if (slot.type == effectchain::Synth) {
            e.settings = synthjson::toJson(track.synth);
        } else if (slot.type == effectchain::BassGuitar) {
            e.settings = bassjson::toJson(track.bass);
        } else if (slot.type == effectchain::GuitarSuite) {
            e.settings = guitarSuite.is_object() ? guitarSuite : json::object();
        }
        else if (mixfx::find(slot.type)) {
            e.settings = mixfxjson::toJson(track.mixEffect(slot.type));
        }
        chain.effects.push_back(std::move(e));
    }
    return chain;
}

bool planImport(const Track& target, const ChainFile& chain, ImportPlan& plan, std::string& error)
{
    ImportPlan p;
    // The target's own pages: they stay whatever the file says, and only they take page settings.
    std::vector<std::string> fixed;
    for (const auto& slot : target.effects)
        if (!effectchain::canRemove(slot.type)) fixed.push_back(slot.type);

    std::vector<std::string> fromFile;                       // the file's effects that apply, in order
    for (const auto& e : chain.effects) {
        const auto* def = effectchain::find(e.type);
        const std::string title = titleOf(e.type);
        const bool page = !effectchain::canRemove(e.type);
        if (page && !contains(fixed, e.type)) { p.skipped.push_back(title); continue; }
        if (!page && (!def || !def->addable)) { p.skipped.push_back(title); continue; }

        std::string why;
        const auto bad = [&](const std::string& reason) {
            error = "The " + title + " settings in the FX chain do not read" + (reason.empty() ? "." : ": " + reason);
            return false;
        };
        const json& s = e.settings;
        if (e.type == effectchain::Eq) {
            std::array<float, 10> gains{};
            if (s.contains("gains")) {
                const auto& g = s["gains"];
                if (!g.is_array() || g.size() != gains.size()) return bad("");
                for (std::size_t i = 0; i < gains.size(); ++i) {
                    if (!g[i].is_number()) return bad("");
                    const double v = g[i].get<double>();
                    if (!std::isfinite(v) || v < TenBandEq::MinGainDb || v > TenBandEq::MaxGainDb) return bad("");
                    gains[i] = static_cast<float>(v);
                }
            }
            p.eqGainsDb = gains;
        } else if (e.type == effectchain::ChaosEq) {
            chaoseq::Settings v;
            if (!chaoseqjson::fromJson(s, v, why)) return bad(why);
            p.chaosEq = v;
        } else if (e.type == effectchain::Harmonium) {
            harmonium::Settings v;
            if (!harmoniumjson::fromJson(s, v, why)) return bad(why);
            p.harmonium = v;
        } else if (e.type == effectchain::DrumKit) {
            drumkit::Settings v;
            if (!drumkitjson::fromJson(s, v, why)) return bad(why);
            p.drumkit = v;
        } else if (e.type == effectchain::ElectroDrums) {
            electrodrums::Settings v;
            if (!electrodrumsjson::fromJson(s, v, why)) return bad(why);
            p.electrodrums = v;
        } else if (e.type == effectchain::Piano) {
            piano::Settings v;
            if (!pianojson::fromJson(s, v, why)) return bad(why);
            p.piano = v;
        } else if (e.type == effectchain::Chords) {
            chords::Settings v;
            if (!chordsjson::fromJson(s, v, why)) return bad(why);
            p.chords = v;
        } else if (e.type == effectchain::Synth) {
            synth::Settings v;
            if (!synthjson::fromJson(s, v, why)) return bad(why);
            p.synth = v;
        } else if (e.type == effectchain::BassGuitar) {
            bass::Settings v;
            if (!bassjson::fromJson(s, v, why)) return bad(why);
            p.bass = v;
        } else if (e.type == effectchain::GuitarSuite) {
            if (!s.empty()) p.guitarSuite = s;               // the pedalboard checks itself on load
        }
        else if (mixfx::find(e.type)) {
            mixfx::Settings v;
            if (!mixfxjson::fromJson(e.type, s, v, why)) return bad(why);
            p.mixEffects[e.type] = v;
        }
        fromFile.push_back(e.type);
        p.kept.push_back(title);
        if (!page) p.addTypes.push_back(e.type);
    }
    // The target's pages the file does not mention keep their place at the front.
    for (const auto& type : fixed) if (!contains(fromFile, type)) p.finalOrder.push_back(type);
    for (const auto& type : fromFile) p.finalOrder.push_back(type);

    plan = std::move(p);
    error.clear();
    return true;
}

} // namespace persistence::chainjson
