#pragma once

#include "plugins/chordplayer/chordsettings.h"

#include <nlohmann/json.hpp>
#include <string>

// A Chords track's settings in save files:
//   {"chords": ["Am", "G", "F", "E", "", "", "", ""], "autoplay": 3, "key": 9, "minor": true}
// A missing field keeps its default, so older files load.
namespace persistence::chordsjson {

nlohmann::json toJson(const chords::Settings& settings);

// False, with a message and no change to settings, for anything malformed or out of range.
bool fromJson(const nlohmann::json& json, chords::Settings& settings, std::string& error);

} // namespace persistence::chordsjson
