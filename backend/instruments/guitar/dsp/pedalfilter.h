#pragma once
#include "dsp/biquad.h"
#include "dsp/eqbanddesign.h"

#include <array>

// A linear filter for guitar pedals: one EQ band (up to four biquads) with its own state per
// channel. Inactive filters pass audio through. Real-time safe: set() and run() never allocate.
struct PedalFilter {
    eqband::Design design;
    std::array<std::array<biquad::State, eqband::MaxSections>, 2> state{};
    bool active = false;

    void set(const eqband::Settings& settings, double rate) noexcept
    {
        design = eqband::design(settings, rate);
        active = true;
    }

    float run(int channel, float x) noexcept
    {
        if (!active) return x;
        double y = x;
        auto& s = state[std::size_t(channel)];
        for (int k = 0; k < design.sectionCount; ++k)
            y = biquad::process(design.sections[std::size_t(k)], s[std::size_t(k)], y);
        return float(y);
    }

    void reset() noexcept
    {
        for (auto& channel : state) channel = {};
    }
};
