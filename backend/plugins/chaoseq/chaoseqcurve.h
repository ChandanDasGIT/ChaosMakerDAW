#pragma once

#include "plugins/chaoseq/chaoseqsettings.h"

#include <array>
#include <vector>

// What the Chaos EQ display draws: each band's response and the sum of all of them, sampled
// at log-spaced frequencies from 20 Hz to 20 kHz. Computed on the UI side, never on the
// audio thread.
namespace chaoseq {

constexpr double CurveMinHz = 20.0;
constexpr double CurveMaxHz = 20000.0;

// Frequency of point index out of count, log spaced from CurveMinHz to CurveMaxHz.
double curveFrequency(int index, int count) noexcept;

struct Curve {
    std::vector<double> frequencies;
    // Sum of the bands that are in use and not bypassed. Output gain is not included.
    std::vector<double> totalDb;
    // One entry per slot; empty for slots not in use. Bypassed bands still get a curve so the
    // display can show them dimmed.
    std::array<std::vector<double>, MaxBands> bandDb;
};

Curve curve(const Settings& settings, double sampleRate, int points);

} // namespace chaoseq
