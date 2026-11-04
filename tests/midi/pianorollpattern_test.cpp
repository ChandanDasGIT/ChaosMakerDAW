#include "testkit.h"
#include "midi/pianorollpattern.h"

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
