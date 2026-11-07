#pragma once

#include <array>
#include <span>

// The computer keyboard as a MIDI keyboard, in the usual DAW layout:
//
//    w e   t y u   o p          black keys
//   a s d f g h j k l ; '       white keys: C D E F G A B C D E F
//   z / x  octave down / up     c / v  velocity down / up
//
// Keys are lower-case characters. Holding a key does not repeat its note (the computer's key
// repeat is ignored), and releasing a key always ends the note it started, even after the
// octave changed. Pure logic: the wrapper feeds it keys and sends out the notes.
class TypingKeyboard {
public:
    static constexpr int MinOctave = 0, MaxOctave = 8;     // octave 4: 'a' plays C4, MIDI 60
    static constexpr int VelocityStep = 20;

    struct Action {
        enum class Kind { None, NoteOn, NoteOff, Changed } kind = Kind::None;   // Changed: octave/velocity
        int note = -1;
        int velocity = 0;
    };

    // Whether the key belongs to the layout (notes or controls): the caller keeps it from
    // reaching anything else while the typing keyboard is on.
    static bool handles(char key) noexcept;
    // Semitones above the octave's C for a note key, or -1.
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
