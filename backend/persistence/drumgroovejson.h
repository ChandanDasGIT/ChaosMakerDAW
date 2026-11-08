#pragma once

#include "instruments/drums/grooves/drumgroove.h"

#include <nlohmann/json.hpp>
#include <string>

// A drum groove as a file (a user's own groove, saved in the app's data folder):
//   {"format": "ChaosMakerGroove", "version": 1, "name": "My beat", "meter": [7, 8], "bars": 2,
//    "notes": [{"key": 60, "beat": 0, "length": 0.25, "velocity": 112}, ...]}
// Beats are quarter notes from the groove's start.
namespace persistence::drumgroovejson {

constexpr std::size_t MaxFileSize = 512 * 1024;

nlohmann::json toJson(const drumgroove::Groove& groove);

// False, with a message and no change to `groove`, for anything malformed or out of range.
// A loaded groove is never a factory one.
bool fromJson(const nlohmann::json& json, drumgroove::Groove& groove, std::string& error);

} // namespace persistence::drumgroovejson
