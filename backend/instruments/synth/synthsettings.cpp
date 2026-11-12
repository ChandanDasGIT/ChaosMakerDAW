#include "instruments/synth/synthsettings.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace synth {

namespace {

double clampOr(double value, double low, double high, double fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

template <typename E>
E enumOr(E value, E last) noexcept
{
    const int i = int(value);
    return i >= 0 && i <= int(last) ? value : E{};
}

Envelope sanitize(const Envelope& in, const Envelope& d) noexcept
{
    Envelope e;
    e.attack = clampOr(in.attack, MinEnvSeconds, MaxEnvSeconds, d.attack);
    e.decay = clampOr(in.decay, MinEnvSeconds, MaxEnvSeconds, d.decay);
    e.sustain = clampOr(in.sustain, 0.0, 1.0, d.sustain);
    e.release = clampOr(in.release, MinEnvSeconds, MaxEnvSeconds, d.release);
    return e;
}

// One name table per enum, in enum order.
template <typename E, std::size_t N>
std::string_view nameIn(const std::array<std::string_view, N>& names, E value) noexcept
{
    const auto i = std::size_t(value);
    return i < N ? names[i] : names[0];
}

template <typename E, std::size_t N>
bool fromNameIn(const std::array<std::string_view, N>& names, std::string_view name, E& value) noexcept
{
    for (std::size_t i = 0; i < N; ++i)
        if (names[i] == name) { value = E(i); return true; }
    return false;
}

constexpr std::array<std::string_view, 4> waveNames{"saw", "pulse", "triangle", "sine"};
constexpr std::array<std::string_view, 2> subWaveNames{"sine", "square"};
constexpr std::array<std::string_view, 4> filterModeNames{"lowpass12", "lowpass24", "highpass", "bandpass"};
constexpr std::array<std::string_view, 5> lfoShapeNames{"sine", "triangle", "saw", "square", "random"};
constexpr std::array<std::string_view, 4> lfoTargetNames{"pitch", "filter", "volume", "pulsewidth"};
constexpr std::array<std::string_view, 3> playModeNames{"poly", "mono", "legato"};

} // namespace

Settings sanitize(const Settings& in) noexcept
{
    const Settings d;
    Settings s = in;
    for (std::size_t i = 0; i < s.osc.size(); ++i) {
        auto& o = s.osc[i];
        const auto& od = d.osc[i];
        o.wave = enumOr(o.wave, Wave::Sine);
        o.octave = std::clamp(o.octave, -MaxOctave, MaxOctave);
        o.semitones = std::clamp(o.semitones, -MaxSemitones, MaxSemitones);
        o.fineCents = clampOr(o.fineCents, -MaxFineCents, MaxFineCents, od.fineCents);
        o.pulseWidth = clampOr(o.pulseWidth, MinPulseWidth, MaxPulseWidth, od.pulseWidth);
        o.level = clampOr(o.level, 0.0, 1.0, od.level);
    }
    s.subWave = enumOr(s.subWave, SubWave::Square);
    s.subLevel = clampOr(s.subLevel, 0.0, 1.0, d.subLevel);
    s.noiseLevel = clampOr(s.noiseLevel, 0.0, 1.0, d.noiseLevel);

    auto& f = s.filter;
    f.mode = enumOr(f.mode, FilterMode::BandPass);
    f.cutoffHz = clampOr(f.cutoffHz, MinCutoffHz, MaxCutoffHz, d.filter.cutoffHz);
    f.resonance = clampOr(f.resonance, 0.0, 1.0, d.filter.resonance);
    f.drive = clampOr(f.drive, 0.0, 1.0, d.filter.drive);
    f.keyTrack = clampOr(f.keyTrack, 0.0, 1.0, d.filter.keyTrack);
    f.envOctaves = clampOr(f.envOctaves, -MaxFilterEnvOctaves, MaxFilterEnvOctaves, d.filter.envOctaves);
    f.velocity = clampOr(f.velocity, 0.0, 1.0, d.filter.velocity);

    s.filterEnv = sanitize(s.filterEnv, d.filterEnv);
    s.ampEnv = sanitize(s.ampEnv, d.ampEnv);
    s.ampVelocity = clampOr(s.ampVelocity, 0.0, 1.0, d.ampVelocity);

    auto& l = s.lfo;
    l.shape = enumOr(l.shape, LfoShape::Random);
    l.target = enumOr(l.target, LfoTarget::PulseWidth);
    l.rateHz = clampOr(l.rateHz, MinLfoHz, MaxLfoHz, d.lfo.rateHz);
    l.division = std::clamp(l.division, 0, DivisionCount - 1);
    l.depth = clampOr(l.depth, 0.0, 1.0, d.lfo.depth);
    l.delaySeconds = clampOr(l.delaySeconds, 0.0, MaxLfoDelaySeconds, d.lfo.delaySeconds);

    s.mode = enumOr(s.mode, PlayMode::Legato);
    s.glideSeconds = clampOr(s.glideSeconds, 0.0, MaxGlideSeconds, d.glideSeconds);
    s.bendSemitones = std::clamp(s.bendSemitones, 0, MaxBendSemitones);
    s.volumeDb = clampOr(s.volumeDb, MinVolumeDb, MaxVolumeDb, d.volumeDb);
    return s;
}

bool operator==(const Oscillator& a, const Oscillator& b) noexcept
{
    return a.on == b.on && a.wave == b.wave && a.octave == b.octave && a.semitones == b.semitones
        && a.fineCents == b.fineCents && a.pulseWidth == b.pulseWidth && a.level == b.level;
}

bool operator==(const Envelope& a, const Envelope& b) noexcept
{
    return a.attack == b.attack && a.decay == b.decay && a.sustain == b.sustain && a.release == b.release;
}

bool operator==(const Filter& a, const Filter& b) noexcept
{
    return a.mode == b.mode && a.cutoffHz == b.cutoffHz && a.resonance == b.resonance
        && a.drive == b.drive && a.keyTrack == b.keyTrack && a.envOctaves == b.envOctaves
        && a.velocity == b.velocity;
}

bool operator==(const Lfo& a, const Lfo& b) noexcept
{
    return a.shape == b.shape && a.target == b.target && a.rateHz == b.rateHz && a.sync == b.sync
        && a.division == b.division && a.depth == b.depth && a.delaySeconds == b.delaySeconds;
}

bool operator==(const Settings& a, const Settings& b) noexcept
{
    return a.osc == b.osc && a.subWave == b.subWave && a.subLevel == b.subLevel
        && a.noiseLevel == b.noiseLevel && a.filter == b.filter && a.filterEnv == b.filterEnv
        && a.ampEnv == b.ampEnv && a.ampVelocity == b.ampVelocity && a.lfo == b.lfo
        && a.mode == b.mode && a.glideSeconds == b.glideSeconds
        && a.bendSemitones == b.bendSemitones && a.volumeDb == b.volumeDb;
}

std::string_view waveName(Wave v) noexcept { return nameIn(waveNames, v); }
std::string_view subWaveName(SubWave v) noexcept { return nameIn(subWaveNames, v); }
std::string_view filterModeName(FilterMode v) noexcept { return nameIn(filterModeNames, v); }
std::string_view lfoShapeName(LfoShape v) noexcept { return nameIn(lfoShapeNames, v); }
std::string_view lfoTargetName(LfoTarget v) noexcept { return nameIn(lfoTargetNames, v); }
std::string_view playModeName(PlayMode v) noexcept { return nameIn(playModeNames, v); }

bool waveFromName(std::string_view n, Wave& v) noexcept { return fromNameIn(waveNames, n, v); }
bool subWaveFromName(std::string_view n, SubWave& v) noexcept { return fromNameIn(subWaveNames, n, v); }
bool filterModeFromName(std::string_view n, FilterMode& v) noexcept { return fromNameIn(filterModeNames, n, v); }
bool lfoShapeFromName(std::string_view n, LfoShape& v) noexcept { return fromNameIn(lfoShapeNames, n, v); }
bool lfoTargetFromName(std::string_view n, LfoTarget& v) noexcept { return fromNameIn(lfoTargetNames, n, v); }
bool playModeFromName(std::string_view n, PlayMode& v) noexcept { return fromNameIn(playModeNames, n, v); }

namespace {

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

constexpr std::array<std::string_view, DivisionCount> divisionChoices{"0", "1", "2", "3", "4", "5", "6"};

#define SYNTH_CONTROL(KEY, KIND, MIN, MAX, CHOICES, EXPR)                                       \
    Control{KEY, ControlKind::KIND, double(MIN), double(MAX), CHOICES,                          \
            [](const Settings& s) { return toDouble(s.EXPR); },                                 \
            [](Settings& s, double v) { assign(s.EXPR, v); }}
#define SYNTH_NUMBER(KEY, MIN, MAX, EXPR) SYNTH_CONTROL(KEY, Number, MIN, MAX, {}, EXPR)
#define SYNTH_INTEGER(KEY, MIN, MAX, EXPR) SYNTH_CONTROL(KEY, Integer, MIN, MAX, {}, EXPR)
#define SYNTH_TOGGLE(KEY, EXPR) SYNTH_CONTROL(KEY, Toggle, 0, 1, {}, EXPR)
#define SYNTH_CHOICE(KEY, NAMES, EXPR) SYNTH_CONTROL(KEY, Choice, 0, NAMES.size() - 1, NAMES, EXPR)
#define SYNTH_OSC(P, I)                                                                        \
    SYNTH_TOGGLE(P ".on", osc[I].on),                                                          \
    SYNTH_CHOICE(P ".wave", waveNames, osc[I].wave),                                           \
    SYNTH_INTEGER(P ".octave", -MaxOctave, MaxOctave, osc[I].octave),                          \
    SYNTH_INTEGER(P ".semitones", -MaxSemitones, MaxSemitones, osc[I].semitones),              \
    SYNTH_NUMBER(P ".fine", -MaxFineCents, MaxFineCents, osc[I].fineCents),                    \
    SYNTH_NUMBER(P ".pulseWidth", MinPulseWidth, MaxPulseWidth, osc[I].pulseWidth),            \
    SYNTH_NUMBER(P ".level", 0, 1, osc[I].level)
#define SYNTH_ENV(P, E)                                                                        \
    SYNTH_NUMBER(P ".attack", MinEnvSeconds, MaxEnvSeconds, E.attack),                         \
    SYNTH_NUMBER(P ".decay", MinEnvSeconds, MaxEnvSeconds, E.decay),                           \
    SYNTH_NUMBER(P ".sustain", 0, 1, E.sustain),                                               \
    SYNTH_NUMBER(P ".release", MinEnvSeconds, MaxEnvSeconds, E.release)

const std::array controlTable{
    SYNTH_OSC("osc1", 0),
    SYNTH_OSC("osc2", 1),
    SYNTH_CHOICE("sub.wave", subWaveNames, subWave),
    SYNTH_NUMBER("sub.level", 0, 1, subLevel),
    SYNTH_NUMBER("noise", 0, 1, noiseLevel),
    SYNTH_CHOICE("filter.mode", filterModeNames, filter.mode),
    SYNTH_NUMBER("filter.cutoff", MinCutoffHz, MaxCutoffHz, filter.cutoffHz),
    SYNTH_NUMBER("filter.resonance", 0, 1, filter.resonance),
    SYNTH_NUMBER("filter.drive", 0, 1, filter.drive),
    SYNTH_NUMBER("filter.keyTrack", 0, 1, filter.keyTrack),
    SYNTH_NUMBER("filter.envAmount", -MaxFilterEnvOctaves, MaxFilterEnvOctaves, filter.envOctaves),
    SYNTH_NUMBER("filter.velocity", 0, 1, filter.velocity),
    SYNTH_ENV("filterEnv", filterEnv),
    SYNTH_ENV("ampEnv", ampEnv),
    SYNTH_NUMBER("ampVelocity", 0, 1, ampVelocity),
    SYNTH_CHOICE("lfo.shape", lfoShapeNames, lfo.shape),
    SYNTH_CHOICE("lfo.target", lfoTargetNames, lfo.target),
    SYNTH_NUMBER("lfo.rate", MinLfoHz, MaxLfoHz, lfo.rateHz),
    SYNTH_TOGGLE("lfo.sync", lfo.sync),
    SYNTH_CHOICE("lfo.division", divisionChoices, lfo.division),
    SYNTH_NUMBER("lfo.depth", 0, 1, lfo.depth),
    SYNTH_NUMBER("lfo.delay", 0, MaxLfoDelaySeconds, lfo.delaySeconds),
    SYNTH_CHOICE("mode", playModeNames, mode),
    SYNTH_NUMBER("glide", 0, MaxGlideSeconds, glideSeconds),
    SYNTH_INTEGER("bendRange", 0, MaxBendSemitones, bendSemitones),
    SYNTH_NUMBER("volume", MinVolumeDb, MaxVolumeDb, volumeDb),
};

#undef SYNTH_ENV
#undef SYNTH_OSC
#undef SYNTH_CHOICE
#undef SYNTH_TOGGLE
#undef SYNTH_INTEGER
#undef SYNTH_NUMBER
#undef SYNTH_CONTROL

} // namespace

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

} // namespace synth
