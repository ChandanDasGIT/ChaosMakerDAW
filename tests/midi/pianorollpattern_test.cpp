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
