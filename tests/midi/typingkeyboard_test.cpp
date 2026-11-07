#include "testkit.h"
#include "midi/typingkeyboard.h"

#include <array>
#include <string>

using Kind = TypingKeyboard::Action::Kind;

TEST(typingkeyboard, home_row_plays_a_chromatic_octave_from_c4)
{
    TypingKeyboard keys;
    const std::string layout = "awsedftgyhujkolp;'";
    for (int i = 0; i < int(layout.size()); ++i) {
        const auto on = keys.press(layout[std::size_t(i)]);
        CHECK(on.kind == Kind::NoteOn);
        CHECK(on.note == 60 + i);                                  // a = C4 ... ' = F5
        CHECK(on.velocity == 100);
        CHECK(keys.release(layout[std::size_t(i)]).kind == Kind::NoteOff);
    }
    for (const char key : std::string("qrbnm1 ,./")) {
        CHECK(!TypingKeyboard::handles(key));
        CHECK(keys.press(key).kind == Kind::None);
    }
    CHECK(TypingKeyboard::handles('z') && TypingKeyboard::handles('v'));
}

TEST(typingkeyboard, held_keys_do_not_repeat_and_end_the_note_they_started)
{
    TypingKeyboard keys;
    CHECK(keys.press('a').note == 60);
    CHECK(keys.press('a').kind == Kind::None);                     // key repeat while held
    CHECK(keys.press('x').kind == Kind::Changed);                  // octave up while 'a' is held
    CHECK(keys.press('x').kind == Kind::None);                     // repeat does not climb further
    CHECK(keys.octave() == 5);
    const auto off = keys.release('a');
    CHECK(off.kind == Kind::NoteOff && off.note == 60);            // the note it started, not C5
    CHECK(keys.release('a').kind == Kind::None);
    CHECK(keys.release('x').kind == Kind::None);
    CHECK(keys.press('a').note == 72);                             // now C5
}

TEST(typingkeyboard, octave_and_velocity_stay_in_range)
{
    TypingKeyboard keys;
    for (int i = 0; i < 12; ++i) { keys.press('z'); keys.release('z'); }
    CHECK(keys.octave() == TypingKeyboard::MinOctave);
    CHECK(keys.press('a').note == 12);                             // C0
    keys.release('a');
    for (int i = 0; i < 12; ++i) { keys.press('x'); keys.release('x'); }
    CHECK(keys.octave() == TypingKeyboard::MaxOctave);
    CHECK(keys.press('\'').note == 125);                           // F9: the top key still fits MIDI
    keys.release('\'');
    for (int i = 0; i < 10; ++i) { keys.press('c'); keys.release('c'); }
    CHECK(keys.velocity() == 1);
    for (int i = 0; i < 10; ++i) { keys.press('v'); keys.release('v'); }
    CHECK(keys.velocity() == 127);
    CHECK(keys.press('s').velocity == 127);
}

TEST(typingkeyboard, release_all_ends_every_held_note_once)
{
    TypingKeyboard keys;
    keys.press('a'); keys.press('d'); keys.press('g'); keys.press('x');
    CHECK(keys.heldCount() == 3);
    std::array<int, 8> notes{};
    const int n = keys.releaseAll(notes);
    CHECK(n == 3);
    CHECK(notes[0] + notes[1] + notes[2] == 60 + 64 + 67);
    CHECK(keys.heldCount() == 0);
    CHECK(keys.releaseAll(notes) == 0);
    CHECK(keys.release('a').kind == Kind::None);                   // already ended
    CHECK(keys.press('x').kind == Kind::Changed);                  // 'x' was let go as well
}
