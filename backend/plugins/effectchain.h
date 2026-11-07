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
// instrument track, which starts with its instrument's own FX (guitars: GuitarSuite).
namespace effectchain {
inline constexpr std::string_view Eq = "eq";
inline constexpr std::string_view ChaosEq = "chaoseq";   // plan 6.2
inline constexpr std::string_view GuitarSuite = "guitarsuite";

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
