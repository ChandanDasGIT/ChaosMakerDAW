#include "instruments/bass/basspresets.h"

namespace bass {

namespace {

Preset make(const char* name, Settings s)
{
    return {name, sanitize(s), true};
}

// The default bass: round fingers on the neck pickup.
Settings fingerP()
{
    Settings s;
    s.pluckPosition = 0.6;
    s.pickupBlend = 0.15;
    s.tone = 0.6;
    s.compression = 0.35;
    return s;
}

Settings brightJ()
{
    Settings s;
    s.pluckPosition = 0.25;
    s.brightness = 0.7;
    s.pickupBlend = 0.7;
    s.tone = 0.9;
    s.compression = 0.4;
    return s;
}

Settings slap()
{
    Settings s;
    s.style = Style::Slap;
    s.pluckPosition = 0.4;
    s.sustain = 0.45;
    s.brightness = 0.75;
    s.pickupBlend = 0.55;
    s.tone = 1.0;
    s.compression = 0.6;
    s.velocity = 0.5;
    s.volumeDb = -3.0;                              // slaps and pops peak higher than plucks
    return s;
}

Settings palmMuted()
{
    Settings s;
    s.style = Style::Mute;
    s.muteAmount = 0.7;
    s.pluckPosition = 0.35;
    s.pickupBlend = 0.4;
    s.tone = 0.7;
    s.compression = 0.4;
    s.volumeDb = 2.0;                               // muted notes are short: a little more level
    return s;
}

Settings dub()
{
    Settings s;
    s.pluckPosition = 0.9;
    s.sustain = 0.7;
    s.brightness = 0.25;
    s.pickupBlend = 0.0;
    s.tone = 0.3;
    s.compression = 0.5;
    s.slideSeconds = 0.12;
    return s;
}

Settings rockDrive()
{
    Settings s;
    s.pluckPosition = 0.2;
    s.brightness = 0.65;
    s.velocity = 0.4;
    s.pickupBlend = 0.6;
    s.tone = 0.75;
    s.drive = 0.45;
    s.compression = 0.55;
    s.slideSeconds = 0.05;
    s.volumeDb = 4.0;                               // the drive's clipping keeps it under the others
    return s;
}

} // namespace

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> presets{
        make("Finger P-Bass", fingerP()),
        make("Bright J-Bass", brightJ()),
        make("Slap", slap()),
        make("Palm Muted", palmMuted()),
        make("Dub", dub()),
        make("Rock Drive", rockDrive()),
    };
    return presets;
}

std::optional<std::string> cleanPresetName(std::string_view name)
{
    const auto first = name.find_first_not_of(' ');
    if (first == std::string_view::npos) return std::nullopt;
    const auto last = name.find_last_not_of(' ');
    std::string clean(name.substr(first, last - first + 1));
    if (clean.size() > MaxPresetNameLength) return std::nullopt;
    for (unsigned char c : clean)
        if (c < 0x20 || c == 0x7f || c == '/' || c == '\\') return std::nullopt;
    return clean;
}

} // namespace bass
