#pragma once

#include <span>
#include <string_view>

// Everything the user sets on one bass guitar. Plain data: the track keeps one copy, and the
// engine's BassGuitar receives it through BassGuitar::setSettings().
//
//   4 strings (E1 A1 D2 G2, 24 frets) -> pickups (neck / bridge) -> tone -> drive -> compressor
namespace bass {

// Finger: a round pluck. Slap: the thumb slaps the E and A strings and pops the D and G.
// Mute: the palm rests on the strings; notes come out short and thuddy (muteAmount).
enum class Style { Finger, Slap, Mute };

constexpr int StringCount = 4;
constexpr int FretCount = 24;
// The open strings, low to high: E1 A1 D2 G2 (C4 = 60).
constexpr int OpenKeys[StringCount] = {28, 33, 38, 43};
constexpr int LowestKey = 28;                      // E1, the open E string
constexpr int HighestKey = 43 + FretCount;         // G4, the G string's 24th fret
// Keyswitches below the playable range: C0 finger, D0 slap, E0 palm mute.
constexpr int FingerKeyswitch = 12, SlapKeyswitch = 14, MuteKeyswitch = 16;

constexpr double MaxSlideSeconds = 1.0;
constexpr int MaxBendSemitones = 12;
constexpr double MinVolumeDb = -24.0, MaxVolumeDb = 12.0;

struct Settings {
    Style style = Style::Finger;
    double muteAmount = 0.6;                       // 0..1, in Mute style
    bool slapOnHardHits = false;                   // in Finger style, hard hits slap / pop
    int slapVelocity = 110;                        // 1..127: from this velocity up
    bool keyswitches = true;                       // C0 / D0 / E0 change the style

    double pluckPosition = 0.5;                    // 0 = by the bridge (bright) .. 1 = over the neck (round)
    double sustain = 0.5;                          // 0..1: how long the strings ring
    double brightness = 0.5;                       // 0..1: how long the high overtones last
    double velocity = 0.6;                         // 0..1: how much softer and darker soft notes are

    double pickupBlend = 0.4;                      // 0 = neck pickup only .. 1 = bridge pickup only
    double tone = 0.7;                             // 0..1: the tone knob, dark .. open
    double drive = 0.0;                            // 0..1
    double compression = 0.3;                      // 0..1

    double slideSeconds = 0.08;                    // a key played over a held one slides; 0 = never
    int bendSemitones = 2;
    double volumeDb = 0.0;
};

// Every value in range: non-finite numbers fall back to the defaults, unknown enum values to
// the first one. Rejects nothing.
Settings sanitize(const Settings& settings) noexcept;

bool operator==(const Settings&, const Settings&) noexcept;

// Names used by save files and the UI ("finger", "slap", "mute"). Unknown names give false.
std::string_view styleName(Style value) noexcept;
bool styleFromName(std::string_view name, Style& value) noexcept;

// Every setting by a key, for a page that edits one at a time. The keys are the save file's
// ("style", "pickupBlend", "volume", ...: bassjson).
//   Number    a real value, clamped to min..max
//   Integer   a whole number, rounded and clamped
//   Toggle    0 or 1
//   Choice    an index into `choices` (the save file's names)
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

} // namespace bass
