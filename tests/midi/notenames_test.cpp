#include "testkit.h"
#include "midi/notenames.h"

TEST(notenames, names_follow_midi_60_is_c4)
{
    CHECK(notenames::name(60) == "C4");
    CHECK(notenames::name(61) == "C#4");
    CHECK(notenames::name(69) == "A4");
    CHECK(notenames::name(0) == "C-1");
    CHECK(notenames::name(9) == "A-1");
    CHECK(notenames::name(127) == "G9");
    CHECK(notenames::name(-1).empty() && notenames::name(128).empty());
    CHECK(notenames::readout(60) == "C4 60");
    CHECK(notenames::readout(200).empty());
}

TEST(notenames, black_keys_and_octave_starts)
{
    int black = 0;
    for (int note = 60; note < 72; ++note) if (notenames::isBlackKey(note)) ++black;
    CHECK(black == 5);                                          // C# D# F# G# A#
    CHECK(notenames::isBlackKey(61) && notenames::isBlackKey(70));
    CHECK(!notenames::isBlackKey(64) && !notenames::isBlackKey(65)); // E, F
    CHECK(notenames::isOctaveStart(48) && notenames::isOctaveStart(0) && !notenames::isOctaveStart(59));
    CHECK(notenames::pitchClass(71) == 11 && notenames::pitchClass(-5) == -1);
    CHECK(!notenames::isBlackKey(-1) && !notenames::isOctaveStart(128));
}
