#pragma once

#include <array>
#include <string_view>

// Everything the user sets on one harmonium. Plain data: the track keeps one copy, and the
// engine's Harmonium receives it through Harmonium::setSettings().
namespace harmonium {

// Indian hand harmonium (bass / male / female reeds, drones, hand-pumped bellows) or a Western
// reed organ (stops in feet, steadier foot bellows, Vox Humana tremolo).
enum class Style { Indian, Western };

constexpr int DroneCount = 4;
constexpr int NoDrone = -1;
constexpr int MaxTranspose = 12;          // the scale changer, in semitones either way
constexpr double MaxBeatingCents = 20.0;
constexpr double MinVolumeDb = -24.0, MaxVolumeDb = 12.0;
constexpr double MaxSustainSeconds = 4.0;

struct IndianStops {
    bool bass = false;                    // an octave down
    bool male = true;                     // the main reeds
    bool female = false;                  // an octave up, tuned a little sharp: the beating
};

struct WesternStops {
    bool bourdon16 = false;
    bool diapason8 = true;
    bool dulciana8 = false;
    bool principal4 = false;
    bool voxHumana = false;               // tremolo
};

struct Settings {
    Style style = Style::Indian;
    IndianStops indian;
    WesternStops western;
    double bellows = 0.7;                 // air pressure 0..1: level, brightness, a little flat
    double pumping = 0.3;                 // 0..1 how unevenly the bellows are worked
    double beatingCents = 4.0;            // how far apart the reed sets are tuned
    double airNoise = 0.3;                // 0..1
    double keyClick = 0.3;                // 0..1
    double caseResonance = 0.5;           // 0..1 wooden-box colour and the short reflections
    // A released key keeps ringing: full level for half this time, then it fades out over this
    // time. The next key cuts it. 0 = a plain harmonium, the sound stops when the key comes up.
    double sustainSeconds = 1.5;
    int transpose = 0;                    // scale changer
    double volumeDb = 0.0;
    std::array<int, DroneCount> drones{NoDrone, NoDrone, NoDrone, NoDrone};   // MIDI keys
};

// Every value in range: non-finite numbers fall back to the defaults, drone keys outside
// 0..127 become NoDrone. Rejects nothing.
Settings sanitize(const Settings& settings) noexcept;

bool operator==(const IndianStops&, const IndianStops&) noexcept;
bool operator==(const WesternStops&, const WesternStops&) noexcept;
bool operator==(const Settings&, const Settings&) noexcept;

// Names used by save files and the UI. Unknown names give false.
std::string_view styleName(Style style) noexcept;
bool styleFromName(std::string_view name, Style& style) noexcept;

} // namespace harmonium
