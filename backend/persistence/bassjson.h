#pragma once

#include "instruments/bass/basspresets.h"
#include "instruments/bass/basssettings.h"

#include <nlohmann/json.hpp>
#include <string>

// Bass guitar settings in save files (Bass Guitar tracks, .chaosfx chains and bass presets):
//   {"style": "finger", "muteAmount": 0.6, "slapOnHardHits": false, "slapVelocity": 110,
//    "keyswitches": true, "pluckPosition": 0.5, "sustain": 0.5, "brightness": 0.5,
//    "velocity": 0.6, "pickupBlend": 0.4, "tone": 0.7, "drive": 0, "compression": 0.3,
//    "slide": 0.08, "bendRange": 2, "volume": 0}
// Seconds for slide, semitones for bendRange, dB for volume, the rest 0..1. A missing field
// keeps its default, so older files load; an unknown name or a value out of range is an error.
namespace persistence::bassjson {

nlohmann::json toJson(const bass::Settings& settings);

// False, with a message and no change to settings, for anything malformed or out of range.
bool fromJson(const nlohmann::json& json, bass::Settings& settings, std::string& error);

// A user's bass preset as a file (.basspreset in the app's data folder):
//   {"format": "ChaosMakerBassPreset", "version": 1, "name": "My bass", "settings": {...above...}}
constexpr std::size_t MaxPresetFileSize = 256 * 1024;

nlohmann::json presetToJson(const bass::Preset& preset);
// False, with a message and no change to `preset`, for a file that is not a bass preset, is
// too new, has a bad name or settings that do not read. A loaded preset is never a factory one.
bool presetFromJson(const nlohmann::json& json, bass::Preset& preset, std::string& error);

} // namespace persistence::bassjson
