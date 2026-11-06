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
