#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

// One insert effect on a track. A track's effects run top to bottom in list order (plan 6.1).
struct TrackEffectSlot {
    int id = 0;          // unique within the project; a handle for remove/move, not saved
    std::string type;    // an EffectDefinition::type, e.g. "eq"
};

struct EffectDefinition {
    std::string_view type;
    std::string_view title;
    std::string_view subtitle;
    bool removable = true;   // false: the effect belongs to the instrument (GuitarSuite)
    bool addable = true;     // false: only ever placed by initialEffects(), never by the user
};

// Rules for which effects a track has and may change. No track starts with effects except an
// instrument track, which starts with its instrument's own page (guitars: GuitarSuite,
// harmonium: Harmonium; InstrumentDefinition::page).
namespace effectchain {
inline constexpr std::string_view Eq = "eq";
inline constexpr std::string_view ChaosEq = "chaoseq";   // plan 6.2
inline constexpr std::string_view GuitarSuite = "guitarsuite";
// The harmonium's controls. A page only: the instrument itself plays first, before the effects.
inline constexpr std::string_view Harmonium = "harmonium";
// The acoustic drum kit's controls (room, levels, character), a page like the harmonium's.
inline constexpr std::string_view DrumKit = "drumkit";
// The electronic drum kit's controls (style, per-drum tune / decay / level), the same way.
inline constexpr std::string_view ElectroDrums = "electrodrums";
// The grand piano's controls (tone, pedal, scale changer), the same way.
inline constexpr std::string_view Piano = "piano";
// The Chords track's eight chord strips and autoplay; the grand piano's page follows it.
inline constexpr std::string_view Chords = "chords";
// The mixing plugins (restMap G7) use their mixfx type names: "compressor", ...

// Every effect type, in the order an "Add FX" picker lists them.
std::span<const EffectDefinition> effects();
const EffectDefinition* find(std::string_view type);

// What a new track of this type starts with.
std::vector<std::string> initialEffects(std::string_view trackType);
// Whether the user may add this effect to the chain: an addable type not already on it
// (the EQ keeps one set of bands per track, so each type appears at most once).
bool canAdd(std::span<const TrackEffectSlot> chain, std::string_view effectType);
// The effect types canAdd() allows, in picker order.
std::vector<std::string_view> addableEffects(std::span<const TrackEffectSlot> chain);
bool canRemove(std::string_view effectType);
}
