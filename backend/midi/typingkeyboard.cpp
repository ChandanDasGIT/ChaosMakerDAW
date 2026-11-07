#include "midi/typingkeyboard.h"

#include <algorithm>

namespace {

constexpr bool known(char key) noexcept { return key > 0; }
constexpr int ControlHeld = 1000;                          // a held z/x/c/v: no note

} // namespace

int TypingKeyboard::semitoneFor(char key) noexcept
{
    switch (key) {
    // white keys, lower Pa .. upper Ma            black keys between them
    case '\t': return -5;                          case '1': return -4;
    case 'q': return -3;                           case '2': return -2;
    case 'w': return -1;
    case 'e': return 0;                            case '4': return 1;
    case 'r': return 2;                            case '5': return 3;
    case 't': return 4;
    case 'y': return 5;                            case '7': return 6;
    case 'u': return 7;                            case '8': return 8;
    case 'i': return 9;                            case '9': return 10;
    case 'o': return 11;
    case 'p': return 12;                           case '-': return 13;
    case '[': return 14;                           case '=': return 15;
    case ']': return 16;
    case '\b': return 17;                          case '\\': return 18;
    default: return NoNote;
    }
}

bool TypingKeyboard::handles(char key) noexcept
{
    return semitoneFor(key) != NoNote || key == 'z' || key == 'x' || key == 'c' || key == 'v';
}

TypingKeyboard::Action TypingKeyboard::press(char key) noexcept
{
    if (!known(key)) return {};
    auto& held = m_held[std::size_t(key)];
    if (held) return {};                                   // the computer's key repeat
    switch (key) {
    case 'z': held = ControlHeld; m_octave = std::max(MinOctave, m_octave - 1); return {Action::Kind::Changed};
    case 'x': held = ControlHeld; m_octave = std::min(MaxOctave, m_octave + 1); return {Action::Kind::Changed};
    case 'c': held = ControlHeld; m_velocity = std::max(1, m_velocity - VelocityStep); return {Action::Kind::Changed};
    case 'v': held = ControlHeld; m_velocity = std::min(127, m_velocity + VelocityStep); return {Action::Kind::Changed};
    default: break;
    }
    const int semitone = semitoneFor(key);
    if (semitone == NoNote) return {};
    const int note = 12 * (m_octave + 1) + semitone;       // octave 4: Sa = C4 = 60
    if (note < 0 || note > 127) return {};
    held = note + 1;
    return {Action::Kind::NoteOn, note, m_velocity};
}

TypingKeyboard::Action TypingKeyboard::release(char key) noexcept
{
    if (!known(key)) return {};
    auto& held = m_held[std::size_t(key)];
    if (!held) return {};
    const int note = held - 1;
    const bool control = held == ControlHeld;
    held = 0;
    if (control) return {};
    return {Action::Kind::NoteOff, note, 0};
}

int TypingKeyboard::releaseAll(std::span<int> out) noexcept
{
    int count = 0;
    for (auto& held : m_held) {
        if (!held) continue;
        if (held != ControlHeld && count < int(out.size())) out[std::size_t(count++)] = held - 1;
        held = 0;
    }
    return count;
}

int TypingKeyboard::heldCount() const noexcept
{
    return int(std::count_if(m_held.begin(), m_held.end(), [](int h) { return h != 0 && h != ControlHeld; }));
}
