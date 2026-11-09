#include "testkit.h"
#include "plugins/chordplayer/chordsymbol.h"

#include <set>

using namespace chords;

namespace {

std::string names(const std::vector<Chord>& list)
{
    std::string out;
    for (const auto& c : list) out += (out.empty() ? "" : " ") + c.name;
    return out;
}

} // namespace

TEST(chordsymbol, chord_names_read_in_every_usual_spelling)
{
    const auto c = parse("C");
    CHECK(c && c->root == 0 && c->intervals == (std::vector<int>{0, 4, 7}) && c->name == "C" && c->bass == -1);
    const auto am7 = parse("Am7");
    CHECK(am7 && am7->root == 9 && am7->intervals == (std::vector<int>{0, 3, 7, 10}) && am7->name == "Am7");
    CHECK(parse("Amin7") && parse("Amin7")->name == "Am7");                 // one spelling on the strip
    CHECK(parse("A-7") && *parse("A-7") == *am7);
    CHECK(parse("am7") && parse("am7")->name == "Am7");                    // a lower-case root reads too
    CHECK(parse("  F#m7b5 ") && parse("F#m7b5")->root == 6 && parse("F#m7b5")->intervals == (std::vector<int>{0, 3, 6, 10}));
    CHECK(parse("F#ø") && *parse("F#ø") == *parse("F#m7b5"));     // ø
    CHECK(parse("Bb") && parse("Bb")->root == 10 && parse("Bb")->name == "Bb");
    CHECK(parse("B♭") && parse("B♭")->name == "Bb");               // ♭
    CHECK(parse("Dsus4") && parse("Dsus4")->intervals == (std::vector<int>{0, 5, 7}));
    CHECK(parse("Dsus") && parse("Dsus")->name == "Dsus4");
    CHECK(parse("Cmaj7") && parse("CM7") && parse("CΔ") && *parse("CM7") == *parse("Cmaj7") && *parse("CΔ") == *parse("Cmaj7"));
    CHECK(parse("Edim") && parse("E°") && *parse("E°") == *parse("Edim"));
    CHECK(parse("G7") && parse("G7")->intervals == (std::vector<int>{0, 4, 7, 10}));
    CHECK(parse("Cadd9") && parse("Cadd9")->intervals.back() == 14);
    CHECK(parse("C+") && parse("C+")->name == "Caug");
    CHECK(parse("Cm(maj7)") && parse("Cm(maj7)")->name == "CmMaj7");

    const auto gb = parse("G/B");
    CHECK(gb && gb->root == 7 && gb->bass == 11 && gb->bassClass() == 11 && gb->name == "G/B");
    CHECK(parse("C/C") && parse("C/C")->bass == -1 && parse("C/C")->name == "C");   // its own root: no slash
    CHECK(parse("Am/G") && parse("Am/G")->name == "Am/G");

    for (const char* bad : {"", "H", "Cx7", "C/", "C/H", "C7/Bbb", "Am7b9", "mC", "C  m", "7"})
        CHECK(!parse(bad));
}

TEST(chordsymbol, keys_give_their_chords_by_degree_and_roman_numeral)
{
    const Key cMajor{0, false}, aMinor{9, true}, fMajor{5, false}, dMinor{2, true};
    CHECK(keyName(cMajor) == "C major" && keyName(aMinor) == "A minor" && keyName({10, false}) == "A#/Bb major");
    CHECK(keyName({3, false}) == "D#/Eb major" && keyName({6, true}) == "F#/Gb minor");
    // The key list's names: the same whichever key is chosen, both spellings on the black keys.
    const std::array<std::string, 12> labels{"C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"};
    for (int pc = 0; pc < 12; ++pc) CHECK(tonicLabel(pc) == labels[std::size_t(pc)] && tonicLabel(pc + 12) == labels[std::size_t(pc)]);
    // Chords keep the key's own spelling (D#/Eb major: Eb, Fm, Gm ...).
    CHECK(romanChord(Key{3, false}, "I")->name == "Eb" && romanChord(Key{3, false}, "ii")->name == "Fm");
    CHECK(!usesFlats(cMajor) && usesFlats(fMajor) && usesFlats(dMinor) && !usesFlats(aMinor));

    std::vector<Chord> major;
    for (int d = 1; d <= 7; ++d) major.push_back(*degreeChord(cMajor, d));
    CHECK(names(major) == "C Dm Em F G Am Bdim");
    std::vector<Chord> sevenths;
    for (int d = 1; d <= 7; ++d) sevenths.push_back(*degreeChord(cMajor, d, true));
    CHECK(names(sevenths) == "Cmaj7 Dm7 Em7 Fmaj7 G7 Am7 Bm7b5");
    std::vector<Chord> minor;
    for (int d = 1; d <= 7; ++d) minor.push_back(*degreeChord(aMinor, d));
    CHECK(names(minor) == "Am Bdim C Dm Em F G");
    CHECK(degreeLabels(cMajor)[6] == "vii°" && degreeLabels(aMinor)[0] == "i");
    CHECK(!degreeChord(cMajor, 0) && !degreeChord(cMajor, 8));

    // Spelling follows the key: flats in F major and D minor.
    std::vector<Chord> inF;
    for (int d = 1; d <= 7; ++d) inF.push_back(*degreeChord(fMajor, d));
    CHECK(names(inF) == "F Gm Am Bb C Dm Edim");
    CHECK(romanChord(dMinor, "VI")->name == "Bb");

    CHECK(romanChord(aMinor, "V")->name == "E");                           // the major V in minor
    CHECK(romanChord(aMinor, "V7")->name == "E7");
    CHECK(romanChord(cMajor, "bVII")->name == "Bb");
    CHECK(romanChord(cMajor, "ii7")->name == "Dm7");
    CHECK(romanChord(cMajor, "II7")->name == "D7");
    CHECK(romanChord(cMajor, "IVmaj7")->name == "Fmaj7");
    CHECK(romanChord(cMajor, "viiø7")->name == "Bm7b5");
    CHECK(romanChord(cMajor, "Isus4")->name == "Csus4");
    for (const char* bad : {"", "VIII", "X", "iV7x", "I7b9"}) CHECK(!romanChord(cMajor, bad));
}

TEST(chordsymbol, every_progression_fills_eight_strips_in_any_key_and_both_modes)
{
    CHECK(progressions().size() >= 6);
    for (const bool minor : {false, true}) {
        std::set<std::string_view> seen;
        for (const auto& p : progressions()) {
            CHECK(seen.insert(p.name(minor)).second && !p.description.empty());
            for (int tonic = 0; tonic < 12; ++tonic) CHECK(progressionChords(p, Key{tonic, minor}).size() == 8);
        }
    }
    const auto find = [](std::string_view prefix) {
        for (const auto& p : progressions()) if (p.name(false).starts_with(prefix)) return &p;
        return static_cast<const Progression*>(nullptr);
    };
    const Key cMajor{0, false}, cMinor{0, true};
    // One "Key chords", following the mode.
    CHECK(progressions()[0].name(false) == "Key chords" && progressions()[0].name(true) == "Key chords");
    CHECK(names(progressionChords(progressions()[0], cMajor)) == "C Dm Em F G Am Bdim G7");
    CHECK(names(progressionChords(progressions()[0], cMinor)) == "Cm Ddim Eb Fm Gm Ab Bb G7");
    // The other progressions, counted in the mode's own chords.
    CHECK(find("Pop") && names(progressionChords(*find("Pop"), cMajor)).starts_with("C G Am F"));
    CHECK(find("Pop") && names(progressionChords(*find("Pop"), cMinor)).starts_with("Cm Gm Ab Fm"));
    CHECK(find("Andalusian") && names(progressionChords(*find("Andalusian"), Key{9, true})).starts_with("Am G F E"));
    CHECK(find("Andalusian") && names(progressionChords(*find("Andalusian"), cMajor)).starts_with("C Bdim Am G"));
    CHECK(find("Jazz") && names(progressionChords(*find("Jazz"), cMajor)).starts_with("Dm7 G7 Cmaj7"));
    CHECK(find("Fifties") && names(progressionChords(*find("Fifties"), Key{7, false})).starts_with("G Em C D"));
}

TEST(chordsymbol, the_strips_progression_is_found_or_is_your_own)
{
    const std::array<std::string, 8> keyChords{"C", "Dm", "Em", "F", "G", "Am", "Bdim", "G7"};
    CHECK(matchProgression(keyChords, Key{0, false}) == 0);
    CHECK(matchProgression(keyChords, Key{0, true}) == -1);           // not C minor's
    CHECK(matchProgression(keyChords, Key{2, false}) == -1);          // nor D major's
    const std::array<std::string, 8> pop{"Cm", "Gm", "Ab", "Fm", "Ddim", "Eb", "G7", "Fm7"};
    CHECK(matchProgression(pop, Key{0, true}) == 1);
    auto own = keyChords;
    own[3] = "F7";
    CHECK(matchProgression(own, Key{0, false}) == -1);
}
