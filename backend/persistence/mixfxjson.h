#pragma once

#include "plugins/mixfx/mixfxspec.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

// A mixing plugin's settings in a project file or an FX chain file (restMap G7):
//   {"bypassed": false, "values": {"threshold": -18, "ratio": 4, ...}}
namespace mixfxjson {
nlohmann::json toJson(const mixfx::Settings& settings);
// False, with a message and no change, when the JSON is not settings of this type: not an
// object, bypassed not a bool, values not an object of numbers. Missing or unknown controls are
// fine (defaults / ignored), and values are brought into range.
bool fromJson(std::string_view type, const nlohmann::json& json, mixfx::Settings& settings, std::string& error);
}
