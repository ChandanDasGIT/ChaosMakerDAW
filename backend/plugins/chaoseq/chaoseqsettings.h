#pragma once

#include "plugins/chaoseq/eqbanddesign.h"

#include <array>
#include <string_view>

// Everything the user sets on one Chaos EQ (plan 6.2). Plain data; the track keeps one copy
// and the engine's processors receive it through ChaosEq::setSettings().
namespace chaoseq {

constexpr int MaxBands = 24;
constexpr double MinOutputDb = -24.0;
constexpr double MaxOutputDb = 24.0;

// Which part of a stereo signal a band changes.
enum class Placement { Stereo, Left, Right, Mid, Side };

struct Band {
    bool inUse = false;      // the slot holds a band (a new EQ has none)
    bool bypassed = false;
    bool solo = false;       // listen to only the region this band works on
    Placement placement = Placement::Stereo;
    eqband::Settings filter;
};

struct Settings {
    std::array<Band, MaxBands> bands{};
    double outputGainDb = 0.0;
    bool autoGain = false;   // add autoGainDb() to the output so boosts do not just sound louder
};

// First free slot, or -1 when all 24 are in use.
int firstFreeBand(const Settings& settings) noexcept;
int bandsInUse(const Settings& settings) noexcept;
// The band being soloed: the first slot in use, not bypassed, with solo on. -1 for none.
int soloBand(const Settings& settings) noexcept;

// Every value in range; slots not in use go back to defaults. Rejects nothing.
Settings sanitize(const Settings& settings, double sampleRate) noexcept;

// Output change that roughly cancels the loudness change of the bell, shelf and tilt bands:
// minus their average boost across 20 Hz - 20 kHz on a log scale. Cuts, notches and band
// passes are left out (removing rumble should not make everything else louder).
double autoGainDb(const Settings& settings) noexcept;

// Names used by save files and the UI ("bell", "lowShelf", ...). Unknown names give false.
std::string_view shapeName(eqband::Shape shape) noexcept;
std::string_view placementName(Placement placement) noexcept;
bool shapeFromName(std::string_view name, eqband::Shape& shape) noexcept;
bool placementFromName(std::string_view name, Placement& placement) noexcept;
bool slopeFromDbPerOctave(int dbPerOctave, eqband::Slope& slope) noexcept;

} // namespace chaoseq
