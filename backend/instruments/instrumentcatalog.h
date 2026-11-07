#pragma once

#include <span>
#include <string_view>

struct InstrumentDefinition {
    // Preserve the existing track type strings as IDs, including saved projects/presets.
    std::string_view id;
    std::string_view name;
    bool hasLiveInput = false;
    bool supportsGuitarSuite = false;
    // The instrument's own page in the track's FX panel, an effectchain type: guitars have
    // "guitarsuite", the harmonium "harmonium". Empty while an instrument has no controls yet.
    std::string_view page = {};
};

namespace instrumentcatalog {
// Immutable catalog with process-lifetime storage. Initialize outside the audio callback.
// Entries preserve current choices; an entry does not imply a synthesizer is implemented.
std::span<const InstrumentDefinition> instruments();
const InstrumentDefinition* find(std::string_view id);
}
