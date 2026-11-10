#pragma once

#include <map>
#include <span>
#include <string>
#include <string_view>

// The mixing plugins any track can add (restMap G7: compressor, de-esser, reverb, Chaos Verb).
// Each one is described here once: its controls, their ranges and defaults, and the card each
// sits on in its FX page. Saving, the processors and the page all read this list.
namespace mixfx {

inline constexpr std::string_view Compressor = "compressor";
inline constexpr std::string_view DeEsser = "deesser";
inline constexpr std::string_view Reverb = "reverb";
inline constexpr std::string_view ChaosVerb = "chaosverb";

enum class ControlKind {
    Slider,   // a value between min and max
    Choice,   // an index into choices (min 0, max choices.size() - 1)
    Switch,   // 0 off, 1 on
};

struct Control {
    std::string_view id;
    std::string_view name;
    ControlKind kind = ControlKind::Slider;
    float min = 0.0f;
    float max = 1.0f;
    float def = 0.0f;
    std::string_view unit;                       // "dB", "ms", "Hz", "%", ":1"
    std::span<const std::string_view> choices;   // Choice only
    std::string_view card;                       // the card it sits on in the page
};

struct Spec {
    std::string_view type;
    std::string_view title;
    std::span<const Control> controls;
};

std::span<const Spec> all();
const Spec* find(std::string_view type);
const Control* findControl(const Spec& spec, std::string_view id);

// One plugin's settings on a track: its controls' values by id, and whether it is bypassed.
struct Settings {
    bool bypassed = false;
    std::map<std::string, float, std::less<>> values;
    bool operator==(const Settings&) const = default;
};

// Every control at its default, not bypassed. Empty for an unknown type.
Settings defaults(std::string_view type);
// The settings made valid for the type: unknown ids dropped, missing ones at their default,
// nonfinite values at their default, values clamped, choices and switches rounded to a whole
// index.
Settings sanitize(std::string_view type, const Settings& settings);
// A control's value from settings (its default when missing; 0 for an unknown control).
float value(const Spec& spec, const Settings& settings, std::string_view id);

} // namespace mixfx
