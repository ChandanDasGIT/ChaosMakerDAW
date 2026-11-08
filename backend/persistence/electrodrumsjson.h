#pragma once

#include "instruments/electrodrums/electrosettings.h"

#include <nlohmann/json.hpp>
#include <string>

// Electronic drum kit settings in save files, written for Electronic Drums tracks:
//   {"style": "808", "volume": 0,
//    "drums": {"kick": {"tune": 0, "decay": 1, "level": 0}, "snare": {...}, ...}}
// Volume and levels in dB, tune in semitones, decay a factor. A missing field (or drum) keeps
// its default, so files from older versions load; an unknown drum name is an error.
namespace persistence::electrodrumsjson {

nlohmann::json toJson(const electrodrums::Settings& settings);

// False, with a message and no change to settings, for anything malformed or out of range.
bool fromJson(const nlohmann::json& json, electrodrums::Settings& settings, std::string& error);

} // namespace persistence::electrodrumsjson
