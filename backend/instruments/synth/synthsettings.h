#pragma once

#include <array>
#include <span>
#include <string_view>

// Everything the user sets on one synth. Plain data: the track keeps one copy, and the engine's
// Synth receives it through Synth::setSettings().
//
//   two oscillators + a sub an octave down + noise -> drive -> filter -> amp
//   the filter has its own envelope; one LFO moves pitch, filter, volume or pulse width
namespace synth {

enum class Wave { Saw, Pulse, Triangle, Sine };
enum class SubWave { Sine, Square };
enum class FilterMode { LowPass12, LowPass24, HighPass, BandPass };
enum class LfoShape { Sine, Triangle, Saw, Square, Random };
enum class LfoTarget { Pitch, Filter, Volume, PulseWidth };
// Poly: every key its own voice. Mono: one voice, each key starts the envelopes again.
// Legato: one voice, keys played while another is held glide on without a new attack.
enum class PlayMode { Poly, Mono, Legato };

constexpr int OscillatorCount = 2;
constexpr int MaxOctave = 3;                       // either way
constexpr int MaxSemitones = 12;                   // either way
constexpr double MaxFineCents = 100.0;             // either way
constexpr double MinPulseWidth = 0.05, MaxPulseWidth = 0.95;
constexpr double MinCutoffHz = 20.0, MaxCutoffHz = 20000.0;
constexpr double MaxFilterEnvOctaves = 6.0;        // either way
constexpr double MinEnvSeconds = 0.001, MaxEnvSeconds = 10.0;
constexpr double MinLfoHz = 0.05, MaxLfoHz = 20.0;
constexpr int DivisionCount = 7;                   // tempo::divisionNames
constexpr double MaxLfoDelaySeconds = 5.0;
constexpr double MaxGlideSeconds = 2.0;
constexpr int MaxBendSemitones = 24;
constexpr double MinVolumeDb = -24.0, MaxVolumeDb = 12.0;

// What a full LFO depth does to each target (the depth knob is 0..1; pitch goes by depth squared
// so that small vibrato is easy to set).
constexpr double LfoPitchSemitones = 12.0;
constexpr double LfoFilterOctaves = 4.0;
constexpr double LfoPulseWidth = 0.45;

struct Oscillator {
    bool on = true;
    Wave wave = Wave::Saw;
    int octave = 0;
    int semitones = 0;
    double fineCents = 0.0;
    double pulseWidth = 0.5;                       // Pulse only: share of the cycle that is high
    double level = 0.8;                            // 0..1
};

// Times in seconds. Attack is a straight rise; decay and release reach 1 % of the way in their
// time (exponential, like an analogue envelope).
struct Envelope {
    double attack = 0.005;
    double decay = 0.3;
    double sustain = 0.7;                          // 0..1
    double release = 0.3;
};

struct Filter {
    FilterMode mode = FilterMode::LowPass24;
    double cutoffHz = 4000.0;
    double resonance = 0.2;                        // 0..1, near 1 it rings
    double drive = 0.0;                            // 0..1, soft saturation before the filter
    double keyTrack = 0.5;                         // 0..1: 1 = the cutoff follows the key fully
    double envOctaves = 2.0;                       // the filter envelope's reach, +- octaves
    double velocity = 0.3;                         // 0..1: harder keys open the filter more
};

struct Lfo {
    LfoShape shape = LfoShape::Sine;
    LfoTarget target = LfoTarget::Pitch;
    double rateHz = 5.0;
    bool sync = false;                             // follow the project tempo instead of rateHz
    int division = 2;                              // tempo::divisionNames, 2 = 1/4
    double depth = 0.0;                            // 0..1; the mod wheel adds to it
    double delaySeconds = 0.0;                     // fades in after each new note
};

struct Settings {
    std::array<Oscillator, OscillatorCount> osc{
        Oscillator{},
        Oscillator{false, Wave::Saw, 0, 0, 7.0, 0.5, 0.8}};
    SubWave subWave = SubWave::Sine;
    double subLevel = 0.0;                         // 0..1
    double noiseLevel = 0.0;                       // 0..1
    Filter filter;
    Envelope filterEnv{0.005, 0.4, 0.3, 0.4};
    Envelope ampEnv;
    double ampVelocity = 0.5;                      // 0..1: how much softer soft keys are
    Lfo lfo;
    PlayMode mode = PlayMode::Poly;
    double glideSeconds = 0.0;                     // Mono / Legato only
    int bendSemitones = 2;
    double volumeDb = 0.0;
};

// Every value in range: non-finite numbers fall back to the defaults, unknown enum values to
// the first one. Rejects nothing.
Settings sanitize(const Settings& settings) noexcept;

bool operator==(const Oscillator&, const Oscillator&) noexcept;
bool operator==(const Envelope&, const Envelope&) noexcept;
bool operator==(const Filter&, const Filter&) noexcept;
bool operator==(const Lfo&, const Lfo&) noexcept;
bool operator==(const Settings&, const Settings&) noexcept;

// Names used by save files and the UI ("saw", "lowpass24" ...). Unknown names give false.
std::string_view waveName(Wave value) noexcept;
std::string_view subWaveName(SubWave value) noexcept;
std::string_view filterModeName(FilterMode value) noexcept;
std::string_view lfoShapeName(LfoShape value) noexcept;
std::string_view lfoTargetName(LfoTarget value) noexcept;
std::string_view playModeName(PlayMode value) noexcept;
bool waveFromName(std::string_view name, Wave& value) noexcept;
bool subWaveFromName(std::string_view name, SubWave& value) noexcept;
bool filterModeFromName(std::string_view name, FilterMode& value) noexcept;
bool lfoShapeFromName(std::string_view name, LfoShape& value) noexcept;
bool lfoTargetFromName(std::string_view name, LfoTarget& value) noexcept;
bool playModeFromName(std::string_view name, PlayMode& value) noexcept;

// Every setting by a key, for a page that edits one at a time. The keys are the save file's
// paths ("osc1.wave", "filter.cutoff", "lfo.sync", "volume", ...: synthjson).
//   Number    a real value, clamped to min..max
//   Integer   a whole number, rounded and clamped
//   Toggle    0 or 1
//   Choice    an index into `choices` (the save file's names, "saw", "lowpass24" ...); the
//             division's choices are its indexes, "0".."6" (tempo::divisionNames)
enum class ControlKind { Number, Integer, Toggle, Choice };

struct Control {
    std::string_view key;
    ControlKind kind;
    double min, max;
    std::span<const std::string_view> choices;     // Choice only
    double (*get)(const Settings&);
    void (*set)(Settings&, double);                // no clamping: setControl() does that
};

std::span<const Control> controls();
const Control* findControl(std::string_view key) noexcept;
// Sets one control: a number is clamped and rounded as its kind says. False for an unknown key
// or a value that is not finite.
bool setControl(Settings& settings, std::string_view key, double value) noexcept;
// A Choice control by name. False for an unknown key, a control that is not a Choice or a name
// it does not offer.
bool setChoice(Settings& settings, std::string_view key, std::string_view name) noexcept;

} // namespace synth
