#pragma once

#include <array>
#include <span>

// The computer keyboard as a MIDI keyboard, laid out for sargam from lower Pa to upper Ma:
//
//   black:    1   2       4   5       7   8   9       -   =       Backspace
//   white: Tab  q   w   e   r   t   y   u   i   o   p   [   ]   \ (backslash)
//          Pa. Dh. Ni. Sa  Re  Ga  Ma  Pa  Dha Ni  Sa' Re' Ga' Ma'
//   z / x  octave down / up     c / v  velocity down / up
//
// 'e' plays Sa, the octave's C (octave 4: C4, MIDI 60). Keys are characters: letters in lower
// case, '\t' for Tab and '\b' for Backspace. Holding a key does not repeat its note (the
// computer's key repeat is ignored), and releasing a key always ends the note it started, even
// after the octave changed. Pure logic: the wrapper feeds it keys and sends out the notes.
class TypingKeyboard {
public:
    static constexpr int MinOctave = 0, MaxOctave = 8;     // octave 4: 'e' (Sa) plays C4, MIDI 60
    static constexpr int VelocityStep = 20;

    struct Action {
        enum class Kind { None, NoteOn, NoteOff, Changed } kind = Kind::None;   // Changed: octave/velocity
        int note = -1;
        int velocity = 0;
    };

    // Whether the key belongs to the layout (notes or controls): the caller keeps it from
    // reaching anything else while the typing keyboard is on.
    static bool handles(char key) noexcept;
    static constexpr int NoNote = -100;
    // Semitones from the octave's C (Sa) for a note key: Tab is -5 (lower Pa), Backspace 18
    // (upper tivra Ma). NoNote for other keys.
    static int semitoneFor(char key) noexcept;

    Action press(char key) noexcept;
    Action release(char key) noexcept;
    // Ends every held note (the keyboard is turned off, the window loses focus). Writes the notes
    // to end into `out` and returns how many.
    int releaseAll(std::span<int> out) noexcept;

    int octave() const noexcept { return m_octave; }
    int velocity() const noexcept { return m_velocity; }
    int heldCount() const noexcept;

private:
    int m_octave = 4;
    int m_velocity = 100;
    std::array<int, 128> m_held{};                         // per key character: note + 1, 0 = up
};
