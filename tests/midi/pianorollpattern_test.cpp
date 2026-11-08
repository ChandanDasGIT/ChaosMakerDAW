#include "testkit.h"
#include "midi/pianorollpattern.h"
#include <cmath>

TEST(pianorollpattern, add_move_and_delete_notes)
{
    PianoRollPattern pattern;
    CHECK(pattern.notes().empty());

    int n1 = pattern.addNote(60, 0.0, 1.0, 100);
    int n2 = pattern.addNote(64, 1.0, 0.5, 90);
    CHECK(pattern.notes().size() == 2);

    // Move note
    CHECK(pattern.moveNote(n1, 62, 0.5));
    const MidiNote* note1 = pattern.findNote(n1);
    CHECK(note1 != nullptr);
    CHECK(note1->note == 62);
    CHECK(note1->startBeat == 0.5);

    // Select and delete
    pattern.selectNote(n1);
    CHECK(pattern.selectedCount() == 1);
    CHECK(pattern.deleteSelected());
    CHECK(pattern.notes().size() == 1);
    CHECK(pattern.findNote(n1) == nullptr);
    CHECK(pattern.findNote(n2) != nullptr);

    // Select all and clear
    pattern.selectAll();
    CHECK(pattern.selectedCount() == 1);
    pattern.deleteSelected();
    CHECK(pattern.notes().empty());
}

TEST(pianorollpattern, set_notes_keeps_hidden_starts_and_fixes_ids)
{
    PianoRollPattern pattern;
    MidiNote a; a.id = 5; a.note = 200; a.startBeat = -1.5; a.selected = true;
    MidiNote b; b.id = 5; b.note = 60; b.startBeat = 2.0; b.lengthBeats = 0.0; // duplicate id
    pattern.setNotes({a, b});
    CHECK(pattern.notes().size() == 2);
    CHECK(pattern.notes()[0].startBeat == -1.5);       // hidden (left-trimmed) note survives
    CHECK(pattern.notes()[0].note == 127);
    CHECK(!pattern.notes()[0].selected);
    CHECK(pattern.notes()[1].id != pattern.notes()[0].id);
    CHECK(pattern.notes()[1].lengthBeats > 0.0);
    const int fresh = pattern.addNote(62, 0.0, 1.0);    // new ids never collide
    CHECK(fresh != pattern.notes()[0].id && fresh != pattern.notes()[1].id);
}

TEST(pianorollpattern, quantize_moves_starts_to_the_grid)
{
    PianoRollPattern pattern;
    const int a = pattern.addNote(60, 0.3, 0.7);
    const int b = pattern.addNote(62, 1.13, 0.4);
    const int c = pattern.addNote(64, 2.0, 1.0);    // already on the grid

    // Nothing selected: all notes, to the nearest 1/4 beat; lengths unchanged
    CHECK(pattern.quantize(0.25));
    CHECK(pattern.findNote(a)->startBeat == 0.25);
    CHECK(pattern.findNote(b)->startBeat == 1.25);
    CHECK(pattern.findNote(c)->startBeat == 2.0);
    CHECK(pattern.findNote(a)->lengthBeats == 0.7);
    CHECK(!pattern.quantize(0.25));                  // nothing left to move

    // With a selection: only the selected notes move
    CHECK(pattern.moveNote(a, 60, 0.6));
    CHECK(pattern.moveNote(b, 62, 1.4));
    pattern.selectNote(a);
    CHECK(pattern.quantize(1.0));
    CHECK(pattern.findNote(a)->startBeat == 1.0);
    CHECK(pattern.findNote(b)->startBeat == 1.4);

    // Invalid steps are rejected
    CHECK(!pattern.quantize(0.0));
    CHECK(!pattern.quantize(-1.0));
    CHECK(!pattern.quantize(std::nan("")));
}

TEST(pianorollpattern, quantize_leaves_notes_hidden_by_a_left_trim)
{
    PianoRollPattern pattern;
    MidiNote hidden; hidden.note = 60; hidden.startBeat = -0.4; hidden.lengthBeats = 0.5;
    MidiNote shown; shown.note = 62; shown.startBeat = 0.1; shown.lengthBeats = 0.5;
    pattern.setNotes({hidden, shown});
    CHECK(pattern.quantize(0.25));
    CHECK(pattern.notes()[0].startBeat == -0.4);
    CHECK(pattern.notes()[1].startBeat == 0.0);
}

TEST(pianorollpattern, quantize_per_bar_keeps_odd_steps_on_bar_lines)
{
    // Dotted 1/16 (0.375) in 4/4: lines at 0, 0.375 ... 3.75, then the next bar at 4
    PianoRollPattern dotted;
    const int a = dotted.addNote(60, 3.95, 0.5);   // nearer the bar line than 3.75
    const int b = dotted.addNote(62, 3.8, 0.5);    // nearer 3.75
    const int c = dotted.addNote(64, 4.4, 0.5);    // second bar restarts the grid: 4.375
    CHECK(dotted.quantize(0.375, 4.0));
    CHECK(std::abs(dotted.findNote(a)->startBeat - 4.0) < 1e-9);
    CHECK(std::abs(dotted.findNote(b)->startBeat - 3.75) < 1e-9);
    CHECK(std::abs(dotted.findNote(c)->startBeat - 4.375) < 1e-9);

    // Quarter-note triplets (2/3) in 3/4: 2.9 goes to the bar line at 3, not 3.333
    PianoRollPattern triplet;
    const int d = triplet.addNote(60, 2.9, 0.5);
    const int e = triplet.addNote(62, 1.2, 0.5);
    CHECK(triplet.quantize(2.0 / 3.0, 3.0));
    CHECK(std::abs(triplet.findNote(d)->startBeat - 3.0) < 1e-9);
    CHECK(std::abs(triplet.findNote(e)->startBeat - 4.0 / 3.0) < 1e-9);
}

TEST(pianorollpattern, select_in_range_picks_notes_the_box_touches)
{
    PianoRollPattern pattern;
    const int low = pattern.addNote(60, 0.0, 1.0);     // starts before the box, ends inside
    const int tail = pattern.addNote(62, 1.0, 0.5);    // inside
    const int before = pattern.addNote(63, 0.0, 0.25); // ends exactly at the box start: outside
    const int edge = pattern.addNote(64, 2.0, 1.0);    // starts exactly at the box end: outside
    const int high = pattern.addNote(70, 0.5, 0.5);    // pitch above the box
    const int other = pattern.addNote(55, 3.0, 1.0);

    // Bounds in any order; pitch range is inclusive.
    CHECK(pattern.selectInRange(64, 60, 2.0, 0.25) == 2);
    CHECK(pattern.findNote(low)->selected);
    CHECK(pattern.findNote(tail)->selected);
    CHECK(!pattern.findNote(edge)->selected);
    CHECK(!pattern.findNote(before)->selected);
    CHECK(!pattern.findNote(high)->selected);

    // A new box replaces the selection; with addToSelection it is added.
    CHECK(pattern.selectInRange(55, 55, 3.5, 3.6) == 1);
    CHECK(pattern.selectedCount() == 1 && pattern.findNote(other)->selected);
    CHECK(pattern.selectInRange(70, 70, 0.0, 4.0, true) == 1);
    CHECK(pattern.selectedCount() == 2 && pattern.findNote(high)->selected);

    // An empty box clears the selection (or keeps it when adding).
    CHECK(pattern.selectInRange(100, 120, 0.0, 4.0, true) == 0);
    CHECK(pattern.selectedCount() == 2);
    CHECK(pattern.selectInRange(100, 120, 0.0, 4.0) == 0);
    CHECK(pattern.selectedCount() == 0);
}

TEST(pianorollpattern, new_notes_remember_the_last_velocity_set_or_clicked)
{
    PianoRollPattern p;
    CHECK(p.newNoteVelocity() == 100);
    const int a = p.addNote(60, 0, 1, 40), b = p.addNote(62, 1, 1, 90);
    CHECK(p.newNoteVelocity() == 100);                     // adding a note does not change it
    p.setNoteVelocity(a, 55);
    CHECK(p.newNoteVelocity() == 55);                      // the last velocity set
    p.selectNote(b);
    CHECK(p.newNoteVelocity() == 90);                      // the note last clicked
    p.setNoteVelocity(a, 0);
    CHECK(p.newNoteVelocity() == 1);                       // never a silent new note
    p.setNewNoteVelocity(300);
    CHECK(p.newNoteVelocity() == 127);
    p.setNotes({});                                        // another clip: the editor keeps it
    p.clear();
    CHECK(p.newNoteVelocity() == 127);
}
