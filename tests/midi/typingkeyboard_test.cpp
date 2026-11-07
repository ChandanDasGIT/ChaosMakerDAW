#include "testkit.h"
#include "midi/typingkeyboard.h"

#include <array>
#include <string>

using Kind = TypingKeyboard::Action::Kind;

TEST(typingkeyboard, q_row_plays_lower_pa_to_upper_ma_with_e_as_sa)
{
    TypingKeyboard keys;
    // White keys Tab .. Backspace: Pa. Dha. Ni. Sa Re Ga Ma Pa Dha Ni Sa' Re' Ga' Ma'.
    const std::string white = "\tqwertyuiop[]\b";
    const std::array<int, 14> whiteNotes{55, 57, 59, 60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77};
    // Black keys on the number row, and backslash for upper tivra Ma after Backspace.
    const std::string black = "12457" "89-=\\";
    const std::array<int, 10> blackNotes{56, 58, 61, 63, 66, 68, 70, 73, 75, 78};
    const auto plays = [&keys](char key, int note) {
        const auto on = keys.press(key);
        const bool ok = on.kind == Kind::NoteOn && on.note == note && on.velocity == 100;
        return ok && keys.release(key).kind == Kind::NoteOff;
    };
    for (std::size_t i = 0; i < white.size(); ++i) CHECK(plays(white[i], whiteNotes[i]));
    for (std::size_t i = 0; i < black.size(); ++i) CHECK(plays(black[i], blackNotes[i]));
    CHECK(TypingKeyboard::semitoneFor('e') == 0);                  // Sa

    // The old home-row layout and the number keys with no black key above them play nothing.
    for (const char key : std::string("asdfghjkl;'360bnm,./")) {
        CHECK(!TypingKeyboard::handles(key));
        CHECK(keys.press(key).kind == Kind::None);
    }
    CHECK(TypingKeyboard::handles('z') && TypingKeyboard::handles('v'));
}

TEST(typingkeyboard, held_keys_do_not_repeat_and_end_the_note_they_started)
{
    TypingKeyboard keys;
    CHECK(keys.press('e').note == 60);
    CHECK(keys.press('e').kind == Kind::None);                     // key repeat while held
    CHECK(keys.press('x').kind == Kind::Changed);                  // octave up while Sa is held
    CHECK(keys.press('x').kind == Kind::None);                     // repeat does not climb further
    CHECK(keys.octave() == 5);
    const auto off = keys.release('e');
    CHECK(off.kind == Kind::NoteOff && off.note == 60);            // the note it started, not C5
    CHECK(keys.release('e').kind == Kind::None);
    CHECK(keys.release('x').kind == Kind::None);
    CHECK(keys.press('e').note == 72);                             // now C5
}

TEST(typingkeyboard, octave_and_velocity_stay_in_range)
{
    TypingKeyboard keys;
    for (int i = 0; i < 12; ++i) { keys.press('z'); keys.release('z'); }
    CHECK(keys.octave() == TypingKeyboard::MinOctave);
    CHECK(keys.press('e').note == 12);                             // Sa = C0
    keys.release('e');
    CHECK(keys.press('\t').note == 7);                            // lower Pa still fits MIDI
    keys.release('\t');
    for (int i = 0; i < 12; ++i) { keys.press('x'); keys.release('x'); }
    CHECK(keys.octave() == TypingKeyboard::MaxOctave);
    CHECK(keys.press('\\').note == 126);                         // upper tivra Ma still fits MIDI
    keys.release('\\');
    CHECK(keys.press('\b').note == 125);                          // Backspace: upper Ma, just below
    keys.release('\b');
    for (int i = 0; i < 10; ++i) { keys.press('c'); keys.release('c'); }
    CHECK(keys.velocity() == 1);
    for (int i = 0; i < 10; ++i) { keys.press('v'); keys.release('v'); }
    CHECK(keys.velocity() == 127);
    CHECK(keys.press('r').velocity == 127);
}

TEST(typingkeyboard, release_all_ends_every_held_note_once)
{
    TypingKeyboard keys;
    keys.press('e'); keys.press('t'); keys.press('u'); keys.press('x');   // Sa Ga Pa
    CHECK(keys.heldCount() == 3);
    std::array<int, 8> notes{};
    const int n = keys.releaseAll(notes);
    CHECK(n == 3);
    CHECK(notes[0] + notes[1] + notes[2] == 60 + 64 + 67);
    CHECK(keys.heldCount() == 0);
    CHECK(keys.releaseAll(notes) == 0);
    CHECK(keys.release('e').kind == Kind::None);                   // already ended
    CHECK(keys.press('x').kind == Kind::Changed);                  // 'x' was let go as well
}
