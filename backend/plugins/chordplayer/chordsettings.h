#pragma once

#include "plugins/chordplayer/chordplayer.h"
#include "plugins/chordplayer/chordsymbol.h"

#include <array>
#include <string>

// A Chords track's settings: the eight strips' chords (as names, "" for an empty strip), the
// autoplay (0 off, 1 .. 4), the dynamics (all notes the same volume, or the top note louder),
// whether the chord area plays the bass too, and the key the degree buttons and progressions
// use. Saved with the project (persistence/chordsjson). A new track starts on the key chords
// of C major with the Comping autoplay (3), the same volume for every note, bass with chords.
namespace chords {

struct Settings {
    std::array<std::string, 8> chords{"C", "Dm", "Em", "F", "G", "Am", "Bdim", "G7"};
    int autoplay = 3;
    int dynamics = DynamicsEven;
    bool bassWithChords = true;
    Key key{0, false};

    bool operator==(const Settings&) const = default;
};

// Autoplay into 0 .. 4, the dynamics into range, the tonic into 0 .. 11, and every chord name rewritten in its one
// spelling ("Amin7" -> "Am7"); a name that does not read becomes an empty strip.
Settings sanitize(const Settings& settings);

// The strips as the player takes them (an empty or unreadable name: an empty strip).
std::array<ChordPlayer::Strip, 8> playerStrips(const Settings& settings);

// Everything a track's settings set on its player (strips, autoplay, dynamics, bass with
// chords): the one place that does it, for live edits, a loaded project and offline renders.
void applyTo(ChordPlayer& player, const Settings& settings);

} // namespace chords
