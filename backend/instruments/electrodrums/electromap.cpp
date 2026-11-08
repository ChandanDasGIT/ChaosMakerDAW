#include "instruments/electrodrums/electromap.h"

#include <array>

namespace electromap {

namespace {

using electrodrums::Drum;

constexpr std::array<Pad, 16> Pads{{
    {55, Drum::HatClosed, "Hi-hat tight", 0.6},          // Tab
    {56, Drum::HatClosed, "Hi-hat loose", 1.8},          // 1
    {57, Drum::HatOpen, "Hi-hat half-open", 0.4},        // q
    {60, Drum::Kick, "Kick"},                            // e
    {61, Drum::SideStick, "Side stick"},                 // 4
    {62, Drum::Snare, "Snare"},                          // r
    {63, Drum::Clap, "Clap"},                            // 5
    {64, Drum::Snare, "Snare 2"},                        // t (the acoustic kit's snare edge)
    {65, Drum::TomLow, "Tom low"},                       // y
    {66, Drum::HatClosed, "Hi-hat closed"},              // 7
    {67, Drum::TomMid, "Tom mid"},                       // u
    {68, Drum::HatPedal, "Hi-hat pedal"},                // 8
    {69, Drum::TomHigh, "Tom high"},                     // i
    {70, Drum::HatOpen, "Hi-hat open"},                  // 9
    {73, Drum::Crash, "Crash"},
    {75, Drum::Ride, "Ride"},
}};

} // namespace

std::span<const Pad> pads() noexcept { return Pads; }

const Pad* find(int key) noexcept
{
    for (const auto& pad : pads())
        if (pad.key == key) return &pad;
    return nullptr;
}

std::string_view nameOf(int key) noexcept
{
    const auto* pad = find(key);
    return pad ? pad->name : std::string_view{};
}

} // namespace electromap
