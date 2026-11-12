#include "instruments/bass/basssettings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>

namespace bass {

namespace {

double clampOr(double value, double low, double high, double fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

constexpr std::array<std::string_view, 3> styleNames{"finger", "slap", "mute"};

template <typename T>
double toDouble(T value) noexcept
{
    if constexpr (std::is_enum_v<T>) return double(int(value));
    else return double(value);
}

template <typename T>
void assign(T& field, double value) noexcept
{
    if constexpr (std::is_same_v<T, bool>) field = value >= 0.5;
    else if constexpr (std::is_enum_v<T>) field = T(int(std::lround(value)));
    else if constexpr (std::is_integral_v<T>) field = T(std::lround(value));
    else field = T(value);
}

#define BASS_CONTROL(KEY, KIND, MIN, MAX, CHOICES, EXPR)                                        \
    Control{KEY, ControlKind::KIND, double(MIN), double(MAX), CHOICES,                          \
            [](const Settings& s) { return toDouble(s.EXPR); },                                 \
            [](Settings& s, double v) { assign(s.EXPR, v); }}
#define BASS_NUMBER(KEY, MIN, MAX, EXPR) BASS_CONTROL(KEY, Number, MIN, MAX, {}, EXPR)
#define BASS_INTEGER(KEY, MIN, MAX, EXPR) BASS_CONTROL(KEY, Integer, MIN, MAX, {}, EXPR)
#define BASS_TOGGLE(KEY, EXPR) BASS_CONTROL(KEY, Toggle, 0, 1, {}, EXPR)
#define BASS_CHOICE(KEY, NAMES, EXPR) BASS_CONTROL(KEY, Choice, 0, NAMES.size() - 1, NAMES, EXPR)

const std::array controlTable{
    BASS_CHOICE("style", styleNames, style),
    BASS_NUMBER("muteAmount", 0, 1, muteAmount),
    BASS_TOGGLE("slapOnHardHits", slapOnHardHits),
    BASS_INTEGER("slapVelocity", 1, 127, slapVelocity),
    BASS_TOGGLE("keyswitches", keyswitches),
    BASS_NUMBER("pluckPosition", 0, 1, pluckPosition),
    BASS_NUMBER("sustain", 0, 1, sustain),
    BASS_NUMBER("brightness", 0, 1, brightness),
    BASS_NUMBER("velocity", 0, 1, velocity),
    BASS_NUMBER("pickupBlend", 0, 1, pickupBlend),
    BASS_NUMBER("tone", 0, 1, tone),
    BASS_NUMBER("drive", 0, 1, drive),
    BASS_NUMBER("compression", 0, 1, compression),
    BASS_NUMBER("slide", 0, MaxSlideSeconds, slideSeconds),
    BASS_INTEGER("bendRange", 0, MaxBendSemitones, bendSemitones),
    BASS_NUMBER("volume", MinVolumeDb, MaxVolumeDb, volumeDb),
};

#undef BASS_CHOICE
#undef BASS_TOGGLE
#undef BASS_INTEGER
#undef BASS_NUMBER
#undef BASS_CONTROL

} // namespace

Settings sanitize(const Settings& in) noexcept
{
    const Settings d;
    Settings s = in;
    if (int(s.style) < 0 || int(s.style) > int(Style::Mute)) s.style = Style::Finger;
    s.muteAmount = clampOr(s.muteAmount, 0.0, 1.0, d.muteAmount);
    s.slapVelocity = std::clamp(s.slapVelocity, 1, 127);
    s.pluckPosition = clampOr(s.pluckPosition, 0.0, 1.0, d.pluckPosition);
    s.sustain = clampOr(s.sustain, 0.0, 1.0, d.sustain);
    s.brightness = clampOr(s.brightness, 0.0, 1.0, d.brightness);
    s.velocity = clampOr(s.velocity, 0.0, 1.0, d.velocity);
    s.pickupBlend = clampOr(s.pickupBlend, 0.0, 1.0, d.pickupBlend);
    s.tone = clampOr(s.tone, 0.0, 1.0, d.tone);
    s.drive = clampOr(s.drive, 0.0, 1.0, d.drive);
    s.compression = clampOr(s.compression, 0.0, 1.0, d.compression);
    s.slideSeconds = clampOr(s.slideSeconds, 0.0, MaxSlideSeconds, d.slideSeconds);
    s.bendSemitones = std::clamp(s.bendSemitones, 0, MaxBendSemitones);
    s.volumeDb = clampOr(s.volumeDb, MinVolumeDb, MaxVolumeDb, d.volumeDb);
    return s;
}

bool operator==(const Settings& a, const Settings& b) noexcept
{
    return a.style == b.style && a.muteAmount == b.muteAmount && a.slapOnHardHits == b.slapOnHardHits
        && a.slapVelocity == b.slapVelocity && a.keyswitches == b.keyswitches
        && a.pluckPosition == b.pluckPosition && a.sustain == b.sustain && a.brightness == b.brightness
        && a.velocity == b.velocity && a.pickupBlend == b.pickupBlend && a.tone == b.tone
        && a.drive == b.drive && a.compression == b.compression && a.slideSeconds == b.slideSeconds
        && a.bendSemitones == b.bendSemitones && a.volumeDb == b.volumeDb;
}

std::string_view styleName(Style value) noexcept
{
    const auto i = std::size_t(value);
    return i < styleNames.size() ? styleNames[i] : styleNames[0];
}

bool styleFromName(std::string_view name, Style& value) noexcept
{
    for (std::size_t i = 0; i < styleNames.size(); ++i)
        if (styleNames[i] == name) { value = Style(i); return true; }
    return false;
}

std::span<const Control> controls() { return controlTable; }

const Control* findControl(std::string_view key) noexcept
{
    for (const auto& c : controlTable) if (c.key == key) return &c;
    return nullptr;
}

bool setControl(Settings& settings, std::string_view key, double value) noexcept
{
    const auto* c = findControl(key);
    if (!c || !std::isfinite(value)) return false;
    double v = std::clamp(value, c->min, c->max);
    if (c->kind != ControlKind::Number) v = std::round(v);
    c->set(settings, v);
    return true;
}

bool setChoice(Settings& settings, std::string_view key, std::string_view name) noexcept
{
    const auto* c = findControl(key);
    if (!c || c->kind != ControlKind::Choice) return false;
    for (std::size_t i = 0; i < c->choices.size(); ++i)
        if (c->choices[i] == name) { c->set(settings, double(i)); return true; }
    return false;
}

} // namespace bass
