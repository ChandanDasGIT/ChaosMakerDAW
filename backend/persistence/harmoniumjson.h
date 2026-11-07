#pragma once

#include "instruments/harmonium/harmoniumsettings.h"

#include <nlohmann/json.hpp>
#include <string>

// Harmonium settings in save files, written for Harmonium tracks:
//   {"style": "indian",
//    "indian": {"bass": false, "male": true, "female": false},
//    "western": {"bourdon16": false, "diapason8": true, "dulciana8": false,
//                "principal4": false, "voxHumana": false},
//    "bellows": 0.7, "pumping": 0.3, "beating": 4, "airNoise": 0.3, "keyClick": 0.3,
//    "case": 0.5, "transpose": 0, "volume": 0, "drones": [48, 55, -1, -1]}
// A missing field keeps its default, so files from older versions load.
namespace persistence::harmoniumjson {

nlohmann::json toJson(const harmonium::Settings& settings);

// False, with a message and no change to settings, for anything malformed or out of range.
bool fromJson(const nlohmann::json& json, harmonium::Settings& settings, std::string& error);

} // namespace persistence::harmoniumjson
