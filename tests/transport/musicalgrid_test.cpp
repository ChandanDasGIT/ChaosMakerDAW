#include "testkit.h"
#include "transport/musicalgrid.h"

#include <cmath>
#include <limits>

namespace {
bool near(double a, double b) { return std::abs(a - b) < 1e-9; }
MusicalGrid grid(double beatsPerBar, double beatUnit, double step)
{
    MusicalGrid g; g.setMeter(beatsPerBar, beatUnit); g.setStep(step); return g;
}
}

TEST(musicalgrid, snapping_rounds_floors_and_ceils)
{
    const auto g = grid(4, 1, 0.25);
    CHECK(near(g.snap(1.3), 1.25));
    CHECK(near(g.snap(1.4), 1.5));
    CHECK(near(g.snap(1.375), 1.5));                                   // ties round up
    CHECK(near(g.snap(1.3, MusicalGrid::Rounding::Floor), 1.25));
    CHECK(near(g.snap(1.3, MusicalGrid::Rounding::Ceil), 1.5));
    CHECK(near(g.snap(1.25, MusicalGrid::Rounding::Ceil), 1.25));      // on a line: stays
    CHECK(g.snap(-3.0) == 0.0);                                        // never below 0
    CHECK(g.snap(std::numeric_limits<double>::quiet_NaN()) == 0.0);
}

TEST(musicalgrid, odd_steps_restart_at_every_bar_line)
{
    const auto dotted = grid(4, 1, 0.375);                             // dotted 1/16 in 4/4
    CHECK(near(dotted.snap(3.95), 4.0));                               // next bar line is nearer
    CHECK(near(dotted.snap(3.8), 3.75));
    CHECK(near(dotted.snap(4.4), 4.375));                              // bar 2 restarts at 4
    CHECK(near(dotted.snap(3.9, MusicalGrid::Rounding::Floor), 3.75));
    CHECK(near(dotted.snap(3.9, MusicalGrid::Rounding::Ceil), 4.0));   // capped at the bar line
    const auto triplet = grid(3, 1, 2.0 / 3.0);                        // 1/4 triplets in 3/4
    CHECK(near(triplet.snap(2.9), 3.0));
    // No bars (beatsPerBar 0): one grid from beat 0
    const auto plain = grid(0, 1, 0.375);
    CHECK(near(plain.snap(3.95), 4.125));
}

TEST(musicalgrid, matches_a_plain_grid_whenever_the_step_divides_the_bar)
{
    // The timeline's snap modes (bars, beats, beat/2 ... beat/16) in 4/4, 3/4 and 6/8
    const double meters[][2] = {{4, 1}, {3, 1}, {3, 0.5}};
    for (const auto& meter : meters) {
        const double bpb = meter[0], unit = meter[1];
        for (double step : {bpb, unit, unit / 2, unit / 4, unit / 8, unit / 16}) {
            const auto g = grid(bpb, unit, step);
            for (double beat = 0; beat < 13; beat += 0.0371) {
                const double units = beat / step;
                CHECK(near(g.snap(beat), std::round(units) * step));
                CHECK(near(g.snap(beat, MusicalGrid::Rounding::Floor), std::floor(units + 1e-9) * step));
                CHECK(near(g.snap(beat, MusicalGrid::Rounding::Ceil), std::ceil(units - 1e-9) * step));
            }
        }
    }
}

TEST(musicalgrid, no_step_means_no_snapping)
{
    auto g = grid(4, 1, 0);
    CHECK(near(g.snap(1.3), 1.3) && near(g.snapDelta(0.3), 0.3));
    g.setStep(-1);
    CHECK(g.step() == 0.0);
    g.setStep(0.5);
    CHECK(near(g.snapDelta(0.3), 0.5) && near(g.snapDelta(-0.2), 0.0));
}

TEST(musicalgrid, line_spacing_and_on_grid_checks)
{
    const auto straight = grid(4, 1, 0.25);
    CHECK(near(straight.lineSpacing(96, true), 0.25));
    CHECK(near(straight.lineSpacing(16, true), 0.5));
    CHECK(near(straight.lineSpacing(2, true), 1.0));                   // capped at a beat
    CHECK(near(straight.lineSpacing(2, false), 4.0));                  // uncapped: keeps doubling
    CHECK(near(straight.lineSpacing(0, false), 0.25));                 // invalid zoom: no loop
    CHECK(straight.linesPerBar(0.25) == 16 && straight.linesPerBar(0) == 1);
    CHECK(near(grid(4, 1, 0).lineSpacing(96, true), 1.0));             // no step: beat lines
    CHECK(MusicalGrid::isOnGrid(8.0, 4.0) && !MusicalGrid::isOnGrid(8.5, 4.0));
    CHECK(!MusicalGrid::isOnGrid(1.0, 0.0));
    CHECK(MusicalGrid::isOnGrid(4.0 + 1e-8, 4.0, 1e-7) && !MusicalGrid::isOnGrid(4.0 + 1e-5, 4.0, 1e-7));
}

TEST(musicalgrid, invalid_meters_are_ignored)
{
    MusicalGrid g;
    g.setMeter(-1, 1); g.setMeter(4, 0); g.setMeter(std::nan(""), 1);
    CHECK(g.beatsPerBar() == 4.0 && g.beatUnit() == 1.0);
}
