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
    void clearSelection();
    bool deleteSelected();
    // Move note starts to the nearest multiple of stepBeats (lengths unchanged). Acts on the
    // selected notes, or on all notes when none are selected. Notes with a negative start
    // (hidden by a left-trimmed clip) are left alone. Returns false if nothing moved.
    bool quantize(double stepBeats);

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
