#include "plugins/mixfx/mixfxspec.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mixfx {
namespace {

using K = ControlKind;

constexpr std::array<std::string_view, 3> compressorStyles{"Clean", "Smooth", "Punchy"};

// Clean: a peak detector, as set. Smooth: an averaging (RMS) detector and a release that slows
// the longer the voice is held down, like an optical compressor. Punchy: the detector listens
// to the compressor's own output (feedback), which lets transients through.
constexpr std::array<Control, 10> compressorControls{{
    {"style", "Style", K::Choice, 0, 2, 0, "", compressorStyles, "Compressor"},
    {"threshold", "Threshold", K::Slider, -60, 0, -18, "dB", {}, "Compressor"},
    {"ratio", "Ratio", K::Slider, 1, 20, 4, ":1", {}, "Compressor"},
    {"knee", "Knee", K::Slider, 0, 12, 6, "dB", {}, "Compressor"},
    {"attack", "Attack", K::Slider, 0.1f, 100, 10, "ms", {}, "Timing"},
    {"release", "Release", K::Slider, 10, 2000, 150, "ms", {}, "Timing"},
    {"detectorLowCut", "Detector low cut", K::Slider, 20, 300, 80, "Hz", {}, "Timing"},
    {"makeup", "Makeup", K::Slider, 0, 24, 0, "dB", {}, "Output"},
    {"autoMakeup", "Auto makeup", K::Switch, 0, 1, 0, "", {}, "Output"},
    {"mix", "Mix", K::Slider, 0, 100, 100, "%", {}, "Output"},
}};

constexpr std::array<std::string_view, 2> deEsserModes{"Split", "Wide"};

// Split: only the band above Frequency is turned down. Wide: the whole voice dips. Listen plays
// the detected band alone, to find the harshness.
constexpr std::array<Control, 5> deEsserControls{{
    {"frequency", "Frequency", K::Slider, 2000, 12000, 6000, "Hz", {}, "Detection"},
    {"threshold", "Threshold", K::Slider, -60, 0, -30, "dB", {}, "Detection"},
    {"listen", "Listen", K::Switch, 0, 1, 0, "", {}, "Detection"},
    {"range", "Range", K::Slider, 0, 24, 8, "dB", {}, "Reduction"},
    {"mode", "Mode", K::Choice, 0, 1, 0, "", deEsserModes, "Reduction"},
}};

constexpr std::array<std::string_view, 3> reverbTypes{"Room", "Plate", "Hall"};

// A plain reverb: a room, a plate or a hall. Low and high cut shape only the reverb, not the
// dry voice.
constexpr std::array<Control, 9> reverbControls{{
    {"type", "Type", K::Choice, 0, 2, 0, "", reverbTypes, "Space"},
    {"size", "Size", K::Slider, 0, 100, 50, "%", {}, "Space"},
    {"decay", "Decay", K::Slider, 0.2f, 10, 1.5f, "s", {}, "Space"},
    {"preDelay", "Pre-delay", K::Slider, 0, 200, 20, "ms", {}, "Space"},
    {"damping", "Damping", K::Slider, 0, 100, 50, "%", {}, "Tone"},
    {"lowCut", "Low cut", K::Slider, 20, 1000, 120, "Hz", {}, "Tone"},
    {"highCut", "High cut", K::Slider, 1000, 20000, 9000, "Hz", {}, "Tone"},
    {"width", "Width", K::Slider, 0, 100, 100, "%", {}, "Output"},
    {"mix", "Mix", K::Slider, 0, 100, 25, "%", {}, "Output"},
}};

constexpr std::array<std::string_view, 7> chaosVerbModes{
    "Concert Hall", "Bright Hall", "Plate", "Room", "Chamber", "Chorus Space", "Ambience"};
constexpr std::array<std::string_view, 3> chaosVerbColours{"1970s", "1980s", "Now"};

// The lush reverb. Mode is the space, Colour the era (1970s: dark and steady, 1980s: bright and
// moving, Now: full range and clean). Early / late blends the first reflections with the tail;
// Bass and Treble decay multiply how long the lows and highs ring compared with the middle.
constexpr std::array<Control, 15> chaosVerbControls{{
    {"mode", "Mode", K::Choice, 0, 6, 0, "", chaosVerbModes, "Character"},
    {"colour", "Colour", K::Choice, 0, 2, 2, "", chaosVerbColours, "Character"},
    {"mix", "Mix", K::Slider, 0, 100, 30, "%", {}, "Space"},
    {"preDelay", "Pre-delay", K::Slider, 0, 300, 20, "ms", {}, "Space"},
    {"decay", "Decay", K::Slider, 0.2f, 20, 2.5f, "s", {}, "Space"},
    {"size", "Size", K::Slider, 0, 100, 60, "%", {}, "Space"},
    {"earlyLate", "Early / late", K::Slider, 0, 100, 60, "%", {}, "Space"},
    {"diffusion", "Diffusion", K::Slider, 0, 100, 70, "%", {}, "Space"},
    {"width", "Width", K::Slider, 0, 100, 100, "%", {}, "Space"},
    {"modRate", "Mod rate", K::Slider, 0.05f, 5, 0.6f, "Hz", {}, "Movement"},
    {"modDepth", "Mod depth", K::Slider, 0, 100, 50, "%", {}, "Movement"},
    {"highCut", "High cut", K::Slider, 1000, 20000, 9000, "Hz", {}, "Tone"},
    {"lowCut", "Low cut", K::Slider, 20, 1000, 100, "Hz", {}, "Tone"},
    {"bassDecay", "Bass decay", K::Slider, 0.5f, 2, 1.2f, "x", {}, "Tone"},
    {"trebleDecay", "Treble decay", K::Slider, 0.1f, 1, 0.5f, "x", {}, "Tone"},
}};

constexpr std::array<Spec, 4> specs{{
    {Compressor, "Compressor", compressorControls},
    {DeEsser, "De-esser", deEsserControls},
    {Reverb, "Reverb", reverbControls},
    {ChaosVerb, "Chaos Verb", chaosVerbControls},
}};

bool whole(ControlKind kind) { return kind == K::Choice || kind == K::Switch; }

} // namespace

std::span<const Spec> all() { return specs; }

const Spec* find(std::string_view type)
{
    const auto it = std::find_if(specs.begin(), specs.end(), [type](const Spec& s) { return s.type == type; });
    return it == specs.end() ? nullptr : &*it;
}

const Control* findControl(const Spec& spec, std::string_view id)
{
    const auto it = std::find_if(spec.controls.begin(), spec.controls.end(), [id](const Control& c) { return c.id == id; });
    return it == spec.controls.end() ? nullptr : &*it;
}

Settings defaults(std::string_view type)
{
    Settings result;
    if (const auto* spec = find(type))
        for (const auto& control : spec->controls) result.values.emplace(std::string(control.id), control.def);
    return result;
}

Settings sanitize(std::string_view type, const Settings& settings)
{
    Settings result;
    result.bypassed = settings.bypassed;
    const auto* spec = find(type);
    if (!spec) return result;
    for (const auto& control : spec->controls) {
        float v = control.def;
        if (const auto it = settings.values.find(control.id); it != settings.values.end() && std::isfinite(it->second))
            v = std::clamp(it->second, control.min, control.max);
        if (whole(control.kind)) v = std::round(v);
        result.values.emplace(std::string(control.id), v);
    }
    return result;
}

float value(const Spec& spec, const Settings& settings, std::string_view id)
{
    if (const auto it = settings.values.find(id); it != settings.values.end()) return it->second;
    const auto* control = findControl(spec, id);
    return control ? control->def : 0.0f;
}

} // namespace mixfx
