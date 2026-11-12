#include "instruments/synth/synthpresets.h"

namespace synth {

namespace {

Preset make(const char* name, Settings s)
{
    return {name, sanitize(s), true};
}

Settings fatBass()
{
    Settings s;
    s.osc[0] = {true, Wave::Saw, -1, 0, 0.0, 0.5, 0.9};
    s.osc[1] = {true, Wave::Pulse, -1, 0, -8.0, 0.4, 0.6};
    s.subWave = SubWave::Square;
    s.subLevel = 0.6;
    s.filter = {FilterMode::LowPass24, 180.0, 0.35, 0.3, 0.3, 3.5, 0.5};
    s.filterEnv = {0.001, 0.25, 0.1, 0.1};
    s.ampEnv = {0.002, 0.3, 0.8, 0.08};
    s.ampVelocity = 0.4;
    s.mode = PlayMode::Mono;
    return s;
}

Settings soloLead()
{
    Settings s;
    s.osc[0] = {true, Wave::Pulse, 0, 0, 0.0, 0.3, 0.8};
    s.osc[1] = {true, Wave::Saw, 0, 0, 6.0, 0.5, 0.5};
    s.filter = {FilterMode::LowPass24, 2500.0, 0.3, 0.2, 0.5, 1.5, 0.3};
    s.filterEnv = {0.005, 0.4, 0.4, 0.3};
    s.ampEnv = {0.005, 0.2, 0.9, 0.2};
    s.lfo = {LfoShape::Sine, LfoTarget::Pitch, 5.5, false, 2, 0.12, 0.4};
    s.mode = PlayMode::Legato;
    s.glideSeconds = 0.08;
    return s;
}

Settings warmPad()
{
    Settings s;
    s.osc[0] = {true, Wave::Saw, 0, 0, -7.0, 0.5, 0.7};
    s.osc[1] = {true, Wave::Saw, 0, 0, 9.0, 0.5, 0.7};
    s.noiseLevel = 0.03;
    s.filter = {FilterMode::LowPass12, 1500.0, 0.15, 0.0, 0.4, 1.0, 0.2};
    s.filterEnv = {1.2, 2.0, 0.5, 2.0};
    s.ampEnv = {0.8, 1.0, 0.8, 1.5};
    s.ampVelocity = 0.3;
    s.lfo = {LfoShape::Triangle, LfoTarget::Filter, 0.3, false, 2, 0.3, 0.5};
    s.volumeDb = -2.0;                              // chords of two detuned saws
    return s;
}

Settings pluck()
{
    Settings s;
    s.osc[0] = {true, Wave::Saw, 0, 0, 0.0, 0.5, 0.8};
    s.osc[1] = {true, Wave::Pulse, 1, 0, 0.0, 0.5, 0.4};
    s.filter = {FilterMode::LowPass24, 400.0, 0.25, 0.0, 0.6, 4.0, 0.5};
    s.filterEnv = {0.001, 0.15, 0.0, 0.1};
    s.ampEnv = {0.001, 0.6, 0.0, 0.3};
    s.ampVelocity = 0.6;
    s.volumeDb = -2.0;
    return s;
}

Settings brass()
{
    Settings s;
    s.osc[0] = {true, Wave::Saw, 0, 0, -4.0, 0.5, 0.8};
    s.osc[1] = {true, Wave::Saw, 0, 0, 4.0, 0.5, 0.8};
    s.filter = {FilterMode::LowPass24, 700.0, 0.1, 0.2, 0.5, 2.0, 0.6};
    s.filterEnv = {0.08, 0.6, 0.6, 0.3};
    s.ampEnv = {0.04, 0.3, 0.85, 0.25};
    s.ampVelocity = 0.5;
    s.lfo = {LfoShape::Sine, LfoTarget::Pitch, 5.0, false, 2, 0.08, 0.6};
    s.volumeDb = -4.5;                              // two full saws and drive: a chord is loud
    return s;
}

Settings softKeys()
{
    Settings s;
    s.osc[0] = {true, Wave::Triangle, 0, 0, 0.0, 0.5, 0.9};
    s.osc[1] = {true, Wave::Sine, 1, 0, 3.0, 0.5, 0.35};
    s.filter = {FilterMode::LowPass12, 3000.0, 0.0, 0.0, 0.5, 1.0, 0.6};
    s.filterEnv = {0.001, 0.8, 0.2, 0.5};
    s.ampEnv = {0.002, 1.8, 0.25, 0.5};
    s.ampVelocity = 0.7;
    s.lfo = {LfoShape::Sine, LfoTarget::Volume, 4.5, false, 2, 0.15, 0.0};
    return s;
}

Settings strings()
{
    Settings s;
    s.osc[0] = {true, Wave::Saw, 0, 0, -10.0, 0.5, 0.7};
    s.osc[1] = {true, Wave::Saw, 0, 0, 10.0, 0.5, 0.7};
    s.filter = {FilterMode::LowPass12, 2800.0, 0.05, 0.0, 0.5, 0.5, 0.3};
    s.filterEnv = {0.4, 1.0, 0.8, 1.0};
    s.ampEnv = {0.35, 0.5, 0.9, 0.9};
    s.ampVelocity = 0.4;
    s.lfo = {LfoShape::Sine, LfoTarget::Pitch, 5.2, false, 2, 0.07, 0.5};
    return s;
}

Settings wobble()
{
    Settings s;
    s.osc[0] = {true, Wave::Saw, -1, 0, 0.0, 0.5, 0.8};
    s.osc[1] = {true, Wave::Saw, -1, 0, -12.0, 0.5, 0.8};
    s.subLevel = 0.7;
    s.filter = {FilterMode::LowPass24, 150.0, 0.55, 0.4, 0.2, 0.0, 0.0};
    s.ampEnv = {0.005, 0.1, 1.0, 0.1};
    s.lfo = {LfoShape::Sine, LfoTarget::Filter, 5.0, true, 3, 0.75, 0.0};
    s.mode = PlayMode::Mono;
    s.glideSeconds = 0.05;
    return s;
}

} // namespace

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> presets{
        make("Fat Bass", fatBass()),
        make("Solo Lead", soloLead()),
        make("Warm Pad", warmPad()),
        make("Pluck", pluck()),
        make("Brass", brass()),
        make("Soft Keys", softKeys()),
        make("Strings", strings()),
        make("Wobble", wobble()),
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

} // namespace synth
