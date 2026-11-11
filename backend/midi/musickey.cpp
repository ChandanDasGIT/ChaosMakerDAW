#include "midi/musickey.h"

#include <array>
#include <cmath>

namespace musickey {

namespace {

constexpr std::array<std::string_view, 12> Tonics{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr std::array<std::string_view, ScaleCount> Scales{"Major", "Minor", "Harmonic minor", "Major pentatonic",
                                                          "Minor pentatonic", "Chromatic"};
// Each scale as a 12-bit set of steps above the tonic.
constexpr int bits(std::initializer_list<int> steps)
{
    int mask = 0;
    for (const int s : steps) mask |= 1 << s;
    return mask;
}
constexpr std::array<int, ScaleCount> Masks{
    bits({0, 2, 4, 5, 7, 9, 11}), bits({0, 2, 3, 5, 7, 8, 10}), bits({0, 2, 3, 5, 7, 8, 11}),
    bits({0, 2, 4, 7, 9}), bits({0, 3, 5, 7, 10}), 0xFFF};

int mask(Scale scale) noexcept
{
    const int i = int(scale);
    return i >= 0 && i < ScaleCount ? Masks[std::size_t(i)] : 0xFFF;
}

} // namespace

std::span<const std::string_view> tonicNames() { return Tonics; }
std::span<const std::string_view> scaleNames() { return Scales; }

std::string keyName(int tonic, Scale scale)
{
    if (scale == Scale::Chromatic) return "Chromatic";
    std::string name(Tonics[std::size_t(((tonic % 12) + 12) % 12)]);
    std::string kind(Scales[std::size_t(int(scale))]);
    for (auto& c : kind) c = char(std::tolower(static_cast<unsigned char>(c)));
    return name + " " + kind;
}

bool inKey(int note, int tonic, Scale scale) noexcept
{
    const int step = (((note - tonic) % 12) + 12) % 12;
    return (mask(scale) >> step) & 1;
}

Nearest nearest(double midi, int tonic, Scale scale) noexcept
{
    if (!std::isfinite(midi) || std::abs(midi) > 1e6) return {};
    // Every scale here has a note at least every 3 semitones: look 6 either side.
    const int below = int(std::floor(midi));
    int best = -1;
    double bestGap = 1e9;
    for (int candidate = below - 6; candidate <= below + 7; ++candidate) {
        if (!inKey(candidate, tonic, scale)) continue;
        const double gap = std::abs(midi - candidate);
        if (gap < bestGap) { bestGap = gap; best = candidate; }   // ascending: ties keep the lower
    }
    return {best, (midi - best) * 100.0};
}

} // namespace musickey
