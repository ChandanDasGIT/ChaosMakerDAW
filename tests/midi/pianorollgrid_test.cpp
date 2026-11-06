#include "testkit.h"
#include "midi/pianorollgrid.h"

#include <cmath>

namespace {
bool near(double a, double b) { return std::abs(a - b) < 1e-9; }
}

TEST(pianorollgrid, menu_names_and_defaults)
{
    PianoRollGrid grid;
    CHECK(PianoRollGrid::divisionNames().size() == 6 && PianoRollGrid::divisionNames().back() == "Off");
    CHECK(PianoRollGrid::styleNames() == (std::vector<std::string>{"straight", "triplet", "dotted"}));
    CHECK(PianoRollGrid::noteLengthNames().front() == "Grid");
    CHECK(grid.divisionName() == "1/16" && grid.styleName() == "straight" && grid.noteLengthName() == "Grid");
    CHECK(grid.snapEnabled());
    CHECK(!grid.setDivision("1/3") && !grid.setStyle("swing") && !grid.setNoteLength("long"));
    CHECK(grid.divisionName() == "1/16");                       // rejected names change nothing
}

TEST(pianorollgrid, division_and_style_set_the_step)
{
    PianoRollGrid grid;
    CHECK(near(grid.divisionBeats(), 0.25));
    CHECK(grid.setStyle("triplet") && near(grid.divisionBeats(), 1.0 / 6.0));
    CHECK(grid.setStyle("dotted") && near(grid.divisionBeats(), 0.375));
    CHECK(grid.setDivision("1/4") && near(grid.divisionBeats(), 1.5));
    CHECK(grid.setDivision("Off") && grid.divisionBeats() == 0.0);
}

TEST(pianorollgrid, snapping_follows_the_magnet_and_restarts_each_bar)
{
    PianoRollGrid grid;                                          // 1/16 straight, 4/4
    CHECK(near(grid.snapFloor(1.3), 1.25));
    CHECK(near(grid.snapDelta(0.3), 0.25) && near(grid.snapDelta(-0.4), -0.5));
    grid.setSnapEnabled(false);
    CHECK(grid.snapStep() == 0.0);
    CHECK(near(grid.snapFloor(1.3), 1.3) && near(grid.snapDelta(0.3), 0.3));
    CHECK(grid.snapFloor(-2.0) == 0.0);
    grid.setSnapEnabled(true);

    // Dotted 1/16 (0.375) in 4/4: the second bar restarts at 4.0
    grid.setStyle("dotted");
    CHECK(near(grid.snapFloor(3.9), 3.75));
    CHECK(near(grid.snapFloor(4.2), 4.0));
    // Triplet 1/16 (1/6) in 6/8 (3 quarters per bar)
    grid.setStyle("triplet");
    grid.setMeter(3.0, 0.5);
    CHECK(near(grid.snapFloor(0.4), 1.0 / 3.0));
    CHECK(near(grid.snapFloor(3.05), 3.0));
}

TEST(pianorollgrid, note_lengths_and_quantize_step)
{
    PianoRollGrid grid;
    CHECK(near(grid.newNoteLength(), 0.25));                    // Grid = 1/16
    CHECK(grid.setNoteLength("1/4") && near(grid.newNoteLength(), 1.0));
    CHECK(grid.setNoteLength("1/8") && near(grid.newNoteLength(), 0.5));
    CHECK(grid.setNoteLength("Grid") && grid.setDivision("Off"));
    CHECK(near(grid.newNoteLength(), 0.25));                    // Grid Off falls back to 1/4 beat
    CHECK(near(grid.minimumNoteLength(), 0.25));
    grid.setMeter(3.0, 0.5);
    CHECK(near(grid.quantizeStep(), 0.5));                      // Grid Off: one meter beat
    CHECK(grid.setDivision("1/8"));
    grid.setSnapEnabled(false);
    CHECK(near(grid.quantizeStep(), 0.5));                      // quantize ignores the magnet
    CHECK(near(grid.minimumNoteLength(), 0.25));                // resize floor with snap off
}

TEST(pianorollgrid, grid_lines_thin_out_when_zoomed_out)
{
    PianoRollGrid grid;                                          // 1/16 straight, 4/4
    CHECK(near(grid.lineBeats(96), 0.25) && grid.linesPerBar(96) == 16);
    CHECK(near(grid.lineBeats(16), 0.5));                       // 0.25 * 16 px = 4 px < 6: double
    CHECK(near(grid.lineBeats(2), 1.0));                        // never coarser than a beat
    grid.setMeter(3.0, 0.5);                                     // 6/8: never coarser than an eighth
    CHECK(near(grid.lineBeats(2), 0.5) && grid.linesPerBar(2) == 6);
    grid.setMeter(4.0, 1.0);
    grid.setStyle("triplet");                                    // own step, doubled when dense
    CHECK(near(grid.lineBeats(96), 1.0 / 6.0));
    CHECK(near(grid.lineBeats(20), 1.0 / 3.0));
    CHECK(near(grid.lineBeats(0), 1.0 / 6.0));                  // invalid zoom: no endless loop
    grid.setMeter(-1, 0);                                        // invalid meter ignored
    CHECK(grid.beatsPerBar() == 4.0 && grid.beatUnit() == 1.0);
}

TEST(pianorollgrid, on_grid_checks)
{
    CHECK(PianoRollGrid::isOnGrid(1.5, 0.5));
    CHECK(!PianoRollGrid::isOnGrid(1.3, 0.5));
    CHECK(!PianoRollGrid::isOnGrid(1.0, 0.0));
}
