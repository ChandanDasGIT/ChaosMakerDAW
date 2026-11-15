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
    // Kinds of one instrument share a group in the "Add track" picker: Guitar > Electric /
    // Acoustic, Drums > Acoustic / Electronic. `variant` is the name inside the group. Empty
    // for an instrument listed on its own.
    std::string_view group = {};
    std::string_view variant = {};
    // A second page after `page` (the Chords track: its strips, then the grand piano's page).
    std::string_view extraPage = {};
    // Listed in the "Add track" picker's first list, beside Vocal and Instrument (Chords).
    bool ownKind = false;
};

namespace instrumentcatalog {
// Immutable catalog with process-lifetime storage. Initialize outside the audio callback.
// Entries preserve current choices; an entry does not imply a synthesizer is implemented.
std::span<const InstrumentDefinition> instruments();
const InstrumentDefinition* find(std::string_view id);
// Drum kits (the acoustic "Drums", "Electronic Drums"): the piano roll names their keys.
bool isDrumKit(std::string_view id) noexcept;
// The part a drum kit plays on `key` ("Kick", "Clap" ...); "" for other keys and instruments.
std::string_view drumName(std::string_view id, int key) noexcept;
// Instruments whose keys the piano roll names: the drum kits and the Chords track (its strips).
bool namesKeys(std::string_view id) noexcept;
}
