#pragma once

#include "plugins/chaoseq/chaoseqsettings.h"

#include <nlohmann/json.hpp>
#include <string>

// Chaos EQ settings in save files (plan 6.2). Only bands in use are written:
//   {"output": 0, "autoGain": false,
//    "bands": [{"slot": 0, "shape": "bell", "frequency": 1000, "gain": 3, "q": 0.71,
//               "slope": 12, "placement": "stereo", "bypass": false, "solo": false}]}
namespace persistence::chaoseqjson {

nlohmann::json toJson(const chaoseq::Settings& settings);

// False, with a message and no change to settings, for anything malformed or out of range.
bool fromJson(const nlohmann::json& json, chaoseq::Settings& settings, std::string& error);

} // namespace persistence::chaoseqjson
