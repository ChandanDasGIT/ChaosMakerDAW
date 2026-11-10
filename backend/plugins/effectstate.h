#pragma once

#include "plugins/chaoseq/chaoseqsettings.h"
#include "plugins/effectchain.h"
#include "plugins/mixfx/mixfxspec.h"

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The insert effects something carries and their settings: a track, the master track (G9) or
// an audio clip (clip effects, restMap R2). The effect calls work on any of them by its id
// (Project::effects()).
struct EffectState {
    // Insert effects in processing order (plan 6.1); rules in effectchain.
    std::vector<TrackEffectSlot> effects;
    std::array<float, 10> eqGainDb{};
    // Chaos EQ bands (plan 6.2); heard only while "chaoseq" is on the effect list.
    chaoseq::Settings chaosEq;
    // Mixing plugins' settings by type (restMap G7); heard only while that type is on the
    // effect list, kept when it is removed. A type not stored here is at its defaults.
    // (A vector, not a map: Track stays constexpr-constructible.)
    std::vector<std::pair<std::string, mixfx::Settings>> mixEffects;

    mixfx::Settings mixEffect(std::string_view type) const;
    bool hasEffects() const noexcept { return !effects.empty(); }
};
