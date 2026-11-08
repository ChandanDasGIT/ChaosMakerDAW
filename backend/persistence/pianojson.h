#pragma once

#include "instruments/piano/pianosettings.h"

#include <nlohmann/json.hpp>
#include <string>

// Piano settings in save files, written for Piano tracks:
//   {"volume": 0, "release": 0.35, "resonance": 0.5, "velocity": 1, "brightness": 1,
//    "transpose": 0}
// Volume in dB, transpose in semitones, the rest 0..1. A missing field keeps its default, so
// files from older versions load.
namespace persistence::pianojson {

nlohmann::json toJson(const piano::Settings& settings);

// False, with a message and no change to settings, for anything malformed or out of range.
bool fromJson(const nlohmann::json& json, piano::Settings& settings, std::string& error);

} // namespace persistence::pianojson
