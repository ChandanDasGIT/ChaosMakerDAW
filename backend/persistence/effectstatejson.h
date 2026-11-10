#pragma once

#include "plugins/effectstate.h"

#include <nlohmann/json.hpp>

#include <string>

class Project;

// An EffectState (the master's or a clip's effects, restMap G9 / R2) in a project file:
//   {"effects": ["eq", "limiter"], "eq": [10 dB values], "chaosEq": {...},
//    "mixEffects": {"limiter": {...}}}
// Every part is optional when reading (missing: none / flat / defaults).
namespace effectstatejson {
nlohmann::json toJson(const EffectState& state);
// Applies `json` to the effects with id `ownerId` through the project's effect calls, so the
// chain rules and the ranges are checked. False, with a message, when something does not read;
// what was applied before the problem stays (the caller drops the whole document then).
bool apply(const nlohmann::json& json, Project& project, int ownerId, std::string& error);
}
