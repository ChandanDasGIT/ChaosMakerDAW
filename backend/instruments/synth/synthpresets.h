#pragma once
#include "instruments/synth/synthsettings.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Synth presets: a name and a whole Settings. The factory ones are built in and fixed; the
// user's own live as files (SynthPresetStore).
namespace synth {

constexpr std::size_t MaxPresetNameLength = 48;

struct Preset {
    std::string name;
    Settings settings;
    bool factory = false;
};

// Fat Bass, Solo Lead, Warm Pad, Pluck, Brass, Soft Keys, Strings, Wobble (a filter LFO synced
// to 1/8 notes). Built once; every one is already sanitized.
const std::vector<Preset>& factoryPresets();

// The name trimmed of spaces; none when empty, longer than MaxPresetNameLength or holding a
// control character or a slash.
std::optional<std::string> cleanPresetName(std::string_view name);

} // namespace synth
