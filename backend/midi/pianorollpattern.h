#pragma once

#include <vector>
#include <cstdint>

struct MidiNote {
    int id = 0;
    int note = 60; // 0..127 MIDI note number
    double startBeat = 0.0;
    double lengthBeats = 1.0;
    int velocity = 100; // 0..127
    bool selected = false;
};

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

    void clear();

    const std::vector<MidiNote>& notes() const noexcept { return m_notes; }
    int selectedCount() const noexcept;
    const MidiNote* findNote(int noteId) const noexcept;
    MidiNote* findNote(int noteId) noexcept;

private:
    std::vector<MidiNote> m_notes;
    int m_nextId = 1;
};
