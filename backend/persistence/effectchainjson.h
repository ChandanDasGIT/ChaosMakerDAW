#pragma once

#include "instruments/drums/drumkitsettings.h"
#include "instruments/electrodrums/electrosettings.h"
#include "instruments/harmonium/harmoniumsettings.h"
#include "instruments/piano/pianosettings.h"
#include "instruments/synth/synthsettings.h"
#include "plugins/chaoseq/chaoseqsettings.h"
#include "plugins/chordplayer/chordsettings.h"
#include "plugins/mixfx/mixfxspec.h"

#include <map>

#include <nlohmann/json.hpp>

#include <array>
#include <optional>
#include <string>
#include <vector>

struct Track;

// A track's FX chain as a file (.chaosfx), to move a sound from one track or project to another:
//   {"format": "ChaosMakerChain", "version": 1, "trackType": "Electric Guitar",
//    "effects": [{"type": "guitarsuite", "settings": {...the pedalboard...}},
//                {"type": "eq", "settings": {"gains": [0, 0, 3, ...10 values in dB]}},
//                {"type": "chaoseq", "settings": {...chaoseqjson...}}]}
// Effects are in processing order, each type at most once. An instrument page (GuitarSuite,
// Harmonium, Drums, Electronic Drums, Piano, Chords, Synth) carries that instrument's settings; GuitarSuite's
// pedalboard comes from the Qt side (its preset JSON), so capture() takes it as a value.
namespace persistence::chainjson {

constexpr std::size_t MaxFileSize = 4 * 1024 * 1024;
constexpr std::size_t MaxEffects = 16;

struct Effect {
    std::string type;
    nlohmann::json settings = nlohmann::json::object();
};

struct ChainFile {
    std::string trackType;                // the track it came from (shown to the user)
    std::vector<Effect> effects;
};

nlohmann::json toJson(const ChainFile& chain);
// False, with a message and no change to `chain`, for a file that is not a chain file, is too
// new, names an unknown effect or names one twice. Settings are checked by planImport().
bool fromJson(const nlohmann::json& json, ChainFile& chain, std::string& error);

// The track's chain now, in order, with every effect's settings. `guitarSuite` is the
// pedalboard (null when the track has none).
ChainFile capture(const Track& track, const nlohmann::json& guitarSuite = nullptr);

// What importing `chain` onto `target` does: the chain is REPLACED. Effects the user can add
// (EQ, Chaos EQ) come in the file's order; an instrument page's settings apply only when the
// target has the same page (it cannot be added or removed), otherwise it is skipped and named.
// The target's own pages that the file lacks stay, ahead of the rest.
struct ImportPlan {
    std::vector<std::string> addTypes;    // removable effects to add, in this order
    std::vector<std::string> finalOrder;  // the whole chain afterwards, by type
    std::vector<std::string> kept;        // titles of the file's effects that apply
    std::vector<std::string> skipped;     // titles of the file's effects this track can't take
    std::optional<std::array<float, 10>> eqGainsDb;
    std::optional<chaoseq::Settings> chaosEq;
    std::optional<harmonium::Settings> harmonium;
    std::optional<drumkit::Settings> drumkit;
    std::optional<electrodrums::Settings> electrodrums;
    std::optional<piano::Settings> piano;
    std::optional<chords::Settings> chords;
    std::optional<synth::Settings> synth;
    std::optional<nlohmann::json> guitarSuite;
    std::map<std::string, mixfx::Settings> mixEffects;   // restMap G7, by type
};
// False (and nothing planned) when an effect that applies has settings that do not read.
bool planImport(const Track& target, const ChainFile& chain, ImportPlan& plan, std::string& error);

} // namespace persistence::chainjson
