#include "instruments/drums/drummap.h"

#include <array>

namespace drummap {

namespace {

constexpr std::array<Pad, 13> Pads{{
    {55, "Hi-hat tight", "hihatTight"},                // Tab
    {56, "Hi-hat loose", "hihatLoose"},                // 1
    {57, "Hi-hat half-open", "hihatHalfOpen"},         // q
    {60, "Kick", "kick"},                              // e
    {61, "Side stick", "sideStick"},                   // 4
    {62, "Snare", "snare"},                            // r
    {64, "Snare edge", "snareEdge"},                   // t
    {65, "Tom low", "tomLow"},                         // y
    {66, "Hi-hat closed", "hihatClosed"},              // 7
    {67, "Tom mid", "tomMid"},                         // u
    {68, "Hi-hat pedal", "hihatPedal"},                // 8
    {69, "Tom high", "tomHigh"},                       // i
    {70, "Hi-hat open", "hihatOpen"},                  // 9
}};

// The kit's CC 4 ranges for key 46: 0-31 closed, 32-63 loose, 64-95 half-open, 96-127 open.
constexpr std::array<Pass, 6> Passes{{
    {0, -1, -1},                                       // everything else, KitOffset up
    {0, KitTightHihatKey, 55},                         // the kit's tight hat
    {0, KitHihatKey, 66},                              // closed
    {48, KitHihatKey, 56},                             // loose
    {80, KitHihatKey, 57},                             // half-open
    {127, KitHihatKey, 70},                            // open
}};

} // namespace

std::span<const Pad> pads() noexcept { return Pads; }

std::string_view nameOf(int key) noexcept
{
    for (const auto& pad : Pads)
        if (pad.key == key) return pad.name;
    return {};
}

int keyOf(std::string_view id) noexcept
{
    for (const auto& pad : Pads)
        if (pad.id == id) return pad.key;
    return -1;
}

std::span<const Pass> passes() noexcept { return Passes; }

} // namespace drummap
