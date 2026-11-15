#pragma once

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Chords for the Chords track: reading chord names ("C", "Am7", "G/B", "F#m7b5", "Dsus4"),
// the chords of a key by degree (I ii iii IV V vi vii° in major; i ii° III iv v VI VII in
// minor, plus Roman tokens such as "V7", "bVII", "ii7"), and ready-made progressions that fill
// the track's eight chord strips. Pure music theory: no sound, no Qt.
namespace chords {

// A chord: a root pitch class (0 = C .. 11 = B), the intervals above it (0 first, ascending,
// in semitones; 14 = a ninth) and an optional bass pitch class for slash chords.
struct Chord {
    int root = 0;
    std::vector<int> intervals{0, 4, 7};
    int bass = -1;                        // -1: the root is the bass
    std::string name = "C";               // as shown on the strip: "Am7", "G/B", "Bb"

    int bassClass() const noexcept { return bass >= 0 ? bass : root; }
    bool operator==(const Chord& other) const { return root == other.root && intervals == other.intervals && bass == other.bass; }
};

// Reads a chord name. Root A..G (either case) with # / b (or ♯ / ♭), then a quality:
//   (none) maj M  · m min -  · dim ° o  · aug +  · sus2 · sus4 sus · 5 · 6 · m6 · 7 · maj7 M7 Δ
//   · m7 min7 -7 · mMaj7 m(maj7) · dim7 °7 · m7b5 ø ø7 · aug7 7#5 +7 · 7sus4 · 9 · maj9 · m9
//   · add9 · madd9
// then an optional "/bass". The name is rewritten in one spelling ("Amin7" -> "Am7"), keeping
// the root's sharp or flat. Nothing for anything else.
std::optional<Chord> parse(std::string_view text);

// "C", "C#", "Db" ... for a pitch class, with sharps or flats.
std::string noteName(int pitchClass, bool flats);
// A key's tonic as the key list shows it, the same whatever key is chosen: "C", "C#/Db", "D",
// "D#/Eb" ... (both spellings for the black keys).
std::string tonicLabel(int pitchClass);

struct Key {
    int tonic = 0;                        // pitch class
    bool minor = false;                   // natural minor
    bool operator==(const Key&) const = default;
};
// Whether the key is written with flats (F, Bb, Eb ... major; D, G, C ... minor).
bool usesFlats(const Key& key) noexcept;
// "C major", "F#/Gb minor", "A#/Bb major" (the tonic as tonicLabel writes it).
std::string keyName(const Key& key);

// The seven degree labels of the key: {"I","ii","iii","IV","V","vi","vii°"} or the minor ones.
std::array<std::string, 7> degreeLabels(const Key& key);
// The key's own chord on degree 1..7 (triads; `seventh` gives the four-note chord: Imaj7,
// ii7, V7, viiø7 ...). Nothing outside 1..7.
std::optional<Chord> degreeChord(const Key& key, int degree, bool seventh = false);

// A Roman-numeral chord in the key: optional b / # (bVII), I..VII (upper case major, lower
// case minor), then ° (dim), + (aug), ø7 (half-diminished), 7 (V7 dominant, ii7 minor seventh),
// maj7, sus4 or sus2. "V" in a minor key is the major V (E in A minor). Nothing if not read.
std::optional<Chord> romanChord(const Key& key, std::string_view token);

// Eight chords for the strips, from a key and a list of Roman tokens. Every progression has a
// major and a minor form, each counted in that mode's own chords (Pop: I V vi IV in major,
// i v VI iv in minor); the key's mode picks the form.
struct Progression {
    std::array<std::string_view, 2> names;            // major, minor: "Pop I–V–vi–IV", "Pop i–v–VI–iv"
    std::string_view description;
    std::array<std::array<std::string_view, 8>, 2> tokens;   // major, minor
    std::string_view name(bool minor) const noexcept { return names[minor ? 1 : 0]; }
};
std::span<const Progression> progressions();
// The progression's eight chords in the key (its form for the key's mode); empty if a token
// does not read.
std::vector<Chord> progressionChords(const Progression& progression, const Key& key);
// Which progression the eight strips hold in the key, or -1 (chords of your own).
int matchProgression(std::span<const std::string> strips, const Key& key);

} // namespace chords
