#pragma once
#include "instruments/electrodrums/electrovoices.h"

#include <array>
#include <string_view>

// Everything the user sets on one electronic drum kit. Plain data: the track keeps one copy and
// the engine's ElectroDrums receives it through ElectroDrums::setSettings().
namespace electrodrums {

constexpr double MinLevelDb = -24.0, MaxLevelDb = 12.0;

// One drum's knobs: tune and decay as in Knobs, plus its level.
struct DrumSettings {
    double tuneSemitones = 0.0;           // MinTune .. MaxTune
    double decay = 1.0;                   // MinDecay .. MaxDecay, times the recipe's decay
    double levelDb = 0.0;                 // MinLevelDb .. MaxLevelDb
};

struct Settings {
    Style style = Style::Style808;
    std::array<DrumSettings, DrumCount> drums{};   // indexed by Drum
    double volumeDb = 0.0;                         // the whole kit, MinLevelDb .. MaxLevelDb

    DrumSettings& drum(Drum d) noexcept { return drums[std::size_t(d)]; }
    const DrumSettings& drum(Drum d) const noexcept { return drums[std::size_t(d)]; }
};

// Every value in range; non-finite numbers fall back to the defaults.
Settings sanitize(const Settings& settings) noexcept;
bool operator==(const Settings&, const Settings&) noexcept;

// The ids drumId() / styleId() give, back again. Unknown ids give false.
bool styleFromId(std::string_view id, Style& style) noexcept;
bool drumFromId(std::string_view id, Drum& drum) noexcept;

// A drum's name for the page: "Kick", "Hi-hat open" ...
std::string_view drumTitle(Drum drum) noexcept;

} // namespace electrodrums
