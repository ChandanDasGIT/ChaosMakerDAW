#pragma once

#include <vector>
#include <cstdint>
#include "midi/midinote.h"

class PianoRollPattern {
public:
    PianoRollPattern() = default;

    int addNote(int note, double startBeat, double lengthBeats, int velocity = 100);
    bool removeNote(int noteId);
    bool removeNotesAt(int note, double beat);
    bool moveNote(int noteId, int newNote, double newStartBeat);
    bool resizeNote(int noteId, double newLengthBeats);
    bool setNoteVelocity(int noteId, int velocity);

    void selectNote(int noteId, bool addToSelection = false);
    void selectAll(bool select = true);
    // Rubber-band selection: select every note whose pitch is in [lowNote, highNote] and
    // whose span overlaps [startBeat, endBeat) (bounds may come in either order). Other
    // notes are deselected unless addToSelection. Returns how many notes the box touched.
    int selectInRange(int lowNote, int highNote, double startBeat, double endBeat, bool addToSelection = false);
    void clearSelection();
    bool deleteSelected();
    // Move note starts to the nearest grid line (lengths unchanged). Acts on the selected
    // notes, or on all notes when none are selected. Notes with a negative start (hidden by a
    // left-trimmed clip) are left alone. Returns false if nothing moved.
    // With barLengthBeats > 0 the grid restarts at every bar line (so triplet or dotted steps
    // that do not divide the bar still land on bar lines); 0 means one grid from beat 0.
    bool quantize(double stepBeats, double barLengthBeats = 0.0);

    void clear();
    // Replace all notes (e.g. when loading a clip). Start positions are kept as-is, even
    // negative ones (notes hidden by a left-trimmed clip), so writing back never loses them.
    // Pitch/velocity/length are clamped, selection is cleared, and ids are made unique.
    void setNotes(std::vector<MidiNote> notes);

    const std::vector<MidiNote>& notes() const noexcept { return m_notes; }
    int selectedCount() const noexcept;
    const MidiNote* findNote(int noteId) const noexcept;
    MidiNote* findNote(int noteId) noexcept;

private:
    std::vector<MidiNote> m_notes;
    int m_nextId = 1;
};
