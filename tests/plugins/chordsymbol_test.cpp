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
    CHECK(keyName(cMajor) == "C major" && keyName(aMinor) == "A minor" && keyName({10, false}) == "Bb major");
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

TEST(chordsymbol, every_progression_fills_eight_strips_in_any_key)
{
    CHECK(progressions().size() >= 6);
    std::set<std::string_view> seen;
    for (const auto& p : progressions()) {
        CHECK(seen.insert(p.name).second && !p.description.empty());
        for (int tonic = 0; tonic < 12; ++tonic) CHECK(progressionChords(p, tonic).size() == 8);
    }
    const auto find = [](std::string_view prefix) {
        for (const auto& p : progressions()) if (p.name.starts_with(prefix)) return &p;
        return static_cast<const Progression*>(nullptr);
    };
    CHECK(find("Pop") && names(progressionChords(*find("Pop"), 0)).starts_with("C G Am F"));
    CHECK(find("Andalusian") && names(progressionChords(*find("Andalusian"), 9)).starts_with("Am G F E"));
    CHECK(find("Jazz") && names(progressionChords(*find("Jazz"), 0)).starts_with("Dm7 G7 Cmaj7"));
    CHECK(find("Fifties") && names(progressionChords(*find("Fifties"), 7)).starts_with("G Em C D"));
}
