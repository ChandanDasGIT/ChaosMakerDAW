#pragma once

#include "instruments/synth/synthpresets.h"
#include "instruments/synth/synthsettings.h"

#include <nlohmann/json.hpp>
#include <string>

// Synth settings in save files (Synth tracks, .chaosfx chains and synth presets):
//   {"osc1": {"on": true, "wave": "saw", "octave": 0, "semitones": 0, "fine": 0,
//             "pulseWidth": 0.5, "level": 0.8},
//    "osc2": {...},
//    "sub": {"wave": "sine", "level": 0}, "noise": 0,
//    "filter": {"mode": "lowpass24", "cutoff": 4000, "resonance": 0.2, "drive": 0,
//               "keyTrack": 0.5, "envAmount": 2, "velocity": 0.3},
//    "filterEnv": {"attack": 0.005, "decay": 0.4, "sustain": 0.3, "release": 0.4},
//    "ampEnv": {...}, "ampVelocity": 0.5,
//    "lfo": {"shape": "sine", "target": "pitch", "rate": 5, "sync": false, "division": 2,
//            "depth": 0, "delay": 0},
//    "mode": "poly", "glide": 0, "bendRange": 2, "volume": 0}
// Seconds for times, Hz for cutoff and rate, cents for fine, octaves for envAmount, dB for
// volume, the rest 0..1. A missing field keeps its default, so older files load; an unknown
// name or a value out of range is an error.
namespace persistence::synthjson {

nlohmann::json toJson(const synth::Settings& settings);

// False, with a message and no change to settings, for anything malformed or out of range.
bool fromJson(const nlohmann::json& json, synth::Settings& settings, std::string& error);

// A user's synth preset as a file (.synthpreset in the app's data folder):
//   {"format": "ChaosMakerSynthPreset", "version": 1, "name": "My bass", "settings": {...above...}}
constexpr std::size_t MaxPresetFileSize = 256 * 1024;

nlohmann::json presetToJson(const synth::Preset& preset);
// False, with a message and no change to `preset`, for a file that is not a synth preset, is
// too new, has a bad name or settings that do not read. A loaded preset is never a factory one.
bool presetFromJson(const nlohmann::json& json, synth::Preset& preset, std::string& error);

} // namespace persistence::synthjson
