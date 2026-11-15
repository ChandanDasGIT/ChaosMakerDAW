#pragma once

#include <map>

// Everything the user sets on one grand piano. Plain data: the track keeps one copy and the
// engine's Piano receives it through Piano::setSettings().
//
// Release, resonance and velocity are the Splendid map's own controllers (CC 72, 70 and 99,
// read at load time); brightness, volume and the scale changer are added to the zones.
namespace piano {

constexpr double MinVolumeDb = -24.0, MaxVolumeDb = 12.0;
constexpr int MaxTranspose = 12;                    // the scale changer, in semitones either way

// The map's defaults for its controllers (set_hdcc72=0.35, set_hdcc70=0.5, set_hdcc99=1).
constexpr double DefaultRelease = 0.35;
constexpr double DefaultResonance = 0.5;

struct Settings {
    double volumeDb = 0.0;
    double release = DefaultRelease;      // 0..1: up to 2 s more after a key is let go (CC 72)
    double resonance = DefaultResonance;  // 0..1: strings ringing along with the pedal down (CC 70)
    double velocity = 1.0;                // 0..1: how much harder playing is also louder (CC 99)
    double brightness = 1.0;              // 0..1: 1 as recorded, lower is darker (a low-pass)
    int transpose = 0;                    // the scale changer: a played C4 sounds C4 + transpose
};

// Every value in range; non-finite numbers fall back to the defaults.
Settings sanitize(const Settings& settings) noexcept;
bool operator==(const Settings&, const Settings&) noexcept;

// The map's controllers for these settings: CC number -> 0..127.
std::map<int, double> controllers(const Settings& settings);

// The low-pass cutoff for a brightness, in Hz; 0 for none (brightness 1: as recorded).
// From 1 kHz at 0 up to about 20 kHz just below 1.
double brightnessCutoff(double brightness) noexcept;

} // namespace piano
