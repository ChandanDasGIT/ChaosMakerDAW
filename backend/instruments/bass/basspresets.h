#pragma once
#include "instruments/bass/basssettings.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Bass presets: a name and a whole Settings. The factory ones are built in and fixed; the
// user's own live as files (BassPresetStore).
namespace bass {

constexpr std::size_t MaxPresetNameLength = 48;

struct Preset {
    std::string name;
    Settings settings;
    bool factory = false;
};

// Finger P-Bass, Bright J-Bass, Slap, Palm Muted, Dub, Rock Drive. Built once; every one is
// already sanitized.
const std::vector<Preset>& factoryPresets();

// The name trimmed of spaces; none when empty, longer than MaxPresetNameLength or holding a
// control character or a slash.
std::optional<std::string> cleanPresetName(std::string_view name);

} // namespace bass
