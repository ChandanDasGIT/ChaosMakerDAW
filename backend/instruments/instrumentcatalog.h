#pragma once

#include <span>
#include <string_view>

struct InstrumentDefinition {
    // Preserve the existing track type strings as IDs, including saved projects/presets.
    std::string_view id;
    std::string_view name;
    bool hasLiveInput = false;
    bool supportsGuitarSuite = false;
};

namespace instrumentcatalog {
// Immutable catalog with process-lifetime storage. Initialize outside the audio callback.
// Entries preserve current choices; an entry does not imply a synthesizer is implemented.
std::span<const InstrumentDefinition> instruments();
const InstrumentDefinition* find(std::string_view id);
}
