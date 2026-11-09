#include "plugins/chordplayer/chordsettings.h"

#include <algorithm>

namespace chords {

Settings sanitize(const Settings& settings)
{
    Settings s = settings;
    s.autoplay = std::clamp(s.autoplay, 0, AutoplayModes - 1);
    s.variation = s.autoplay == AutoplayOff ? 0 : std::clamp(s.variation, 0, Variations - 1);
    s.dynamics = std::clamp(s.dynamics, 0, DynamicsModes - 1);
    s.key.tonic = ((s.key.tonic % 12) + 12) % 12;
    for (auto& name : s.chords) {
        const auto chord = parse(name);
        name = chord ? chord->name : std::string();
    }
    return s;
}

std::array<ChordPlayer::Strip, 8> playerStrips(const Settings& settings)
{
    std::array<ChordPlayer::Strip, 8> strips{};
    for (std::size_t i = 0; i < strips.size(); ++i)
        if (const auto chord = parse(settings.chords[i])) strips[i] = ChordPlayer::stripOf(*chord);
    return strips;
}

void applyTo(ChordPlayer& player, const Settings& settings)
{
    player.setStrips(playerStrips(settings));
    player.setAutoplay(settings.autoplay);
    player.setVariation(settings.variation);
    player.setDynamics(settings.dynamics);
    player.setBassWithChords(settings.bassWithChords);
}

} // namespace chords
