#include "midi/notenames.h"

#include <array>

namespace {
constexpr std::array<const char*, 12> names{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr std::array<bool, 12> black{false, true, false, true, false, false, true, false, true, false, true, false};
}

bool notenames::isValid(int note) noexcept { return note >= 0 && note <= 127; }

int notenames::pitchClass(int note) noexcept { return isValid(note) ? note % 12 : -1; }

std::string notenames::name(int note)
{
    if (!isValid(note)) return {};
    const int octave = note / 12 - 1;
    return std::string(names[static_cast<std::size_t>(note % 12)]) + std::to_string(octave);
}

bool notenames::isBlackKey(int note) noexcept
{
    return isValid(note) && black[static_cast<std::size_t>(note % 12)];
}

bool notenames::isOctaveStart(int note) noexcept { return pitchClass(note) == 0; }

std::string notenames::readout(int note)
{
    return isValid(note) ? name(note) + " " + std::to_string(note) : std::string();
}
