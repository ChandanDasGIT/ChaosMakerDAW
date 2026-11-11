#include "testkit.h"
#include "midi/musickey.h"

using musickey::Scale;

TEST(musickey, scales_hold_their_notes)
{
    CHECK(musickey::inKey(60, 0, Scale::Major) && musickey::inKey(64, 0, Scale::Major));
    CHECK(!musickey::inKey(61, 0, Scale::Major) && !musickey::inKey(70, 0, Scale::Major));
    CHECK(musickey::inKey(60, 9, Scale::Minor) && !musickey::inKey(68, 9, Scale::Minor));   // A minor: C yes, G# no
    CHECK(musickey::inKey(68, 9, Scale::HarmonicMinor));                                   // A harmonic minor: G#
    CHECK(!musickey::inKey(65, 0, Scale::MajorPentatonic) && musickey::inKey(69, 0, Scale::MajorPentatonic));
    CHECK(musickey::inKey(63, 0, Scale::MinorPentatonic) && !musickey::inKey(62, 0, Scale::MinorPentatonic));
    for (int n = -12; n < 140; ++n) CHECK(musickey::inKey(n, 5, Scale::Chromatic));
    CHECK(musickey::inKey(-1, 11, Scale::Major));                                          // negative notes wrap
    CHECK(musickey::tonicNames().size() == 12 && musickey::scaleNames().size() == musickey::ScaleCount);
    CHECK(musickey::keyName(9, Scale::Minor) == "A minor" && musickey::keyName(1, Scale::Major) == "C# major");
    CHECK(musickey::keyName(4, Scale::Chromatic) == "Chromatic");
}

TEST(musickey, the_nearest_in_key_note_and_its_cents)
{
    auto n = musickey::nearest(60.3, 0, Scale::Major);
    CHECK(n.note == 60);
    CHECK_NEAR(n.cents, 30.0, 1e-9);
    n = musickey::nearest(61.4, 0, Scale::Major);                  // C# is not in C major: D is nearer
    CHECK(n.note == 62);
    CHECK_NEAR(n.cents, -60.0, 1e-9);
    n = musickey::nearest(61.0, 0, Scale::Major);                  // a tie between C and D: the lower
    CHECK(n.note == 60);
    n = musickey::nearest(61.4, 0, Scale::Chromatic);
    CHECK(n.note == 61);
    n = musickey::nearest(66.0, 0, Scale::MajorPentatonic);        // between E 64 and G 67: G
    CHECK(n.note == 67);
    CHECK_NEAR(n.cents, -100.0, 1e-9);
    CHECK(musickey::nearest(std::nan(""), 0, Scale::Major).note == -1);
}
