#include "effectchain.h"
#include "instruments/instrumentcatalog.h"
#include "plugins/mixfx/mixfxspec.h"

#include <algorithm>
#include <array>

namespace {
constexpr std::array<EffectDefinition, 20> catalog{{
    {effectchain::Eq, "Equalizer", "10-band tone shaping", true, true},
    {effectchain::ChaosEq, "Chaos EQ", "Parametric EQ, up to 24 bands", true, true},
    {mixfx::Compressor, "Compressor", "Evens out loud and quiet parts", true, true},
    {mixfx::DeEsser, "De-esser", "Tames harsh s and sh sounds", true, true},
    {mixfx::Reverb, "Reverb", "Room, plate or hall", true, true},
    {mixfx::ChaosVerb, "Chaos Verb", "Lush reverb: spaces, eras and movement", true, true},
    {mixfx::Delay, "Delay", "Echoes in time with the song", true, true},
    {mixfx::Limiter, "Limiter", "A ceiling nothing goes over", true, true},
    {mixfx::Saturation, "Saturation", "Warmth and grit", true, true},
    {mixfx::Gate, "Gate", "Silences noise between phrases", true, true},
    {mixfx::Denoise, "Noise Reduction", "Learns room noise, hiss or hum and removes it", true, true},
    {mixfx::PitchMonitor, "Pitch Monitor", "Shows the sung pitch against the notes of a key", true, true},
    {effectchain::GuitarSuite, "GuitarSuite", "Pedals and signal chain", false, false},
    {effectchain::Harmonium, "Harmonium", "Stops, bellows and drones", false, false},
    {effectchain::DrumKit, "Drum kit", "Room, levels and character", false, false},
    {effectchain::ElectroDrums, "Electronic drums", "Style, tune, decay and levels", false, false},
    {effectchain::Piano, "Grand piano", "Tone, pedal and scale", false, false},
    {effectchain::Chords, "Chords", "Chord strips and autoplay", false, false},
    {effectchain::Synth, "Synth", "Oscillators, filter, envelopes and LFO", false, false},
    {effectchain::BassGuitar, "Bass guitar", "Style, strings, pickups and tone", false, false},
}};

bool onChain(std::span<const TrackEffectSlot> chain, std::string_view type)
{
    return std::any_of(chain.begin(), chain.end(), [type](const auto& slot) { return slot.type == type; });
}
}

std::span<const EffectDefinition> effectchain::effects() { return catalog; }

const EffectDefinition* effectchain::find(std::string_view type)
{
    const auto it = std::find_if(catalog.begin(), catalog.end(), [type](const auto& e) { return e.type == type; });
    return it == catalog.end() ? nullptr : &*it;
}

std::vector<std::string> effectchain::initialEffects(std::string_view trackType)
{
    const auto* instrument = instrumentcatalog::find(trackType);
    std::vector<std::string> pages;
    if (instrument && !instrument->page.empty() && find(instrument->page)) pages.emplace_back(instrument->page);
    if (instrument && !instrument->extraPage.empty() && find(instrument->extraPage)) pages.emplace_back(instrument->extraPage);
    return pages;
}

bool effectchain::canAdd(std::span<const TrackEffectSlot> chain, std::string_view effectType)
{
    const auto* effect = find(effectType);
    return effect && effect->addable && !onChain(chain, effectType);
}

std::vector<std::string_view> effectchain::addableEffects(std::span<const TrackEffectSlot> chain)
{
    std::vector<std::string_view> result;
    for (const auto& effect : catalog)
        if (canAdd(chain, effect.type)) result.push_back(effect.type);
    return result;
}

bool effectchain::canRemove(std::string_view effectType)
{
    const auto* effect = find(effectType);
    return effect && effect->removable;
}
