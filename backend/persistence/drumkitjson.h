#pragma once

#include "instruments/drums/drumkitsettings.h"

#include <nlohmann/json.hpp>
#include <string>

// Drum kit settings in save files, written for Drums tracks:
//   {"room": "front", "kick": 0, "snare": 0, "hihat": 0, "toms": 0, "volume": 0,
//    "tune": 0, "micTiming": 0.496, "kickBeater": 1, "snareBody": 0, "hihatTrash": 0,
//    "tomThud": 0}
// Levels and volume in dB, tune in semitones, the rest 0..1. A missing field keeps its
// default, so files from older versions load.
namespace persistence::drumkitjson {

nlohmann::json toJson(const drumkit::Settings& settings);

// False, with a message and no change to settings, for anything malformed or out of range.
bool fromJson(const nlohmann::json& json, drumkit::Settings& settings, std::string& error);

} // namespace persistence::drumkitjson
