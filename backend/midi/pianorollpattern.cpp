#include "midi/pianorollpattern.h"
#include <algorithm>
#include <cmath>

int PianoRollPattern::addNote(int note, double startBeat, double lengthBeats, int velocity) {
    MidiNote n;
    n.id = m_nextId++;
    n.note = std::clamp(note, 0, 127);
    n.startBeat = std::max(0.0, startBeat);
    n.lengthBeats = std::max(0.01, lengthBeats);
    n.velocity = std::clamp(velocity, 0, 127);
    n.selected = false;
    m_notes.push_back(n);
    return n.id;
}

bool PianoRollPattern::removeNote(int noteId) {
    auto it = std::remove_if(m_notes.begin(), m_notes.end(), [noteId](const MidiNote& n) {
        return n.id == noteId;
    });
    if (it != m_notes.end()) {
        m_notes.erase(it, m_notes.end());
        return true;
    }
    return false;
}

bool PianoRollPattern::removeNotesAt(int note, double beat) {
    auto it = std::remove_if(m_notes.begin(), m_notes.end(), [note, beat](const MidiNote& n) {
        return n.note == note && beat >= n.startBeat && beat <= (n.startBeat + n.lengthBeats);
    });
    if (it != m_notes.end()) {
        m_notes.erase(it, m_notes.end());
        return true;
    }
    return false;
}

bool PianoRollPattern::moveNote(int noteId, int newNote, double newStartBeat) {
    MidiNote* n = findNote(noteId);
    if (!n) return false;
    n->note = std::clamp(newNote, 0, 127);
    n->startBeat = std::max(0.0, newStartBeat);
    return true;
}

bool PianoRollPattern::resizeNote(int noteId, double newLengthBeats) {
    MidiNote* n = findNote(noteId);
    if (!n) return false;
    n->lengthBeats = std::max(0.01, newLengthBeats);
    return true;
}

bool PianoRollPattern::setNoteVelocity(int noteId, int velocity) {
    MidiNote* n = findNote(noteId);
    if (!n) return false;
    n->velocity = std::clamp(velocity, 0, 127);
    return true;
}

void PianoRollPattern::selectNote(int noteId, bool addToSelection) {
    for (auto& n : m_notes) {
        if (n.id == noteId) {
            n.selected = true;
        } else if (!addToSelection) {
            n.selected = false;
        }
    }
}

void PianoRollPattern::selectAll(bool select) {
    for (auto& n : m_notes) {
        n.selected = select;
    }
}

void PianoRollPattern::clearSelection() {
    for (auto& n : m_notes) {
        n.selected = false;
    }
}

bool PianoRollPattern::quantize(double stepBeats, double barLengthBeats) {
    if (!std::isfinite(stepBeats) || stepBeats <= 0) return false;
    const bool perBar = std::isfinite(barLengthBeats) && barLengthBeats > 0;
    const bool onlySelected = selectedCount() > 0;
    constexpr double tolerance = 1e-9;
    bool moved = false;
    for (auto& n : m_notes) {
        if ((onlySelected && !n.selected) || n.startBeat < 0) continue;
        double snapped;
        if (perBar) {
            // Grid lines within the bar, plus the next bar line; take the nearer one.
            const double bar = std::floor(n.startBeat / barLengthBeats + tolerance) * barLengthBeats;
            const double offset = n.startBeat - bar;
            const double lower = std::floor(offset / stepBeats + tolerance) * stepBeats;
            const double upper = std::min(lower + stepBeats, barLengthBeats);
            snapped = bar + (offset - lower < upper - offset ? lower : upper); // ties round up
        } else {
            snapped = std::round(n.startBeat / stepBeats) * stepBeats;
        }
        snapped = std::max(0.0, snapped);
        if (std::abs(snapped - n.startBeat) > tolerance) { n.startBeat = snapped; moved = true; }
    }
    return moved;
}

bool PianoRollPattern::deleteSelected() {
    auto it = std::remove_if(m_notes.begin(), m_notes.end(), [](const MidiNote& n) {
        return n.selected;
    });
    if (it != m_notes.end()) {
        m_notes.erase(it, m_notes.end());
        return true;
    }
    return false;
}

void PianoRollPattern::clear() {
    m_notes.clear();
}

int PianoRollPattern::selectedCount() const noexcept {
    int count = 0;
    for (const auto& n : m_notes) {
        if (n.selected) ++count;
    }
    return count;
}

const MidiNote* PianoRollPattern::findNote(int noteId) const noexcept {
    for (const auto& n : m_notes) {
        if (n.id == noteId) return &n;
    }
    return nullptr;
}

MidiNote* PianoRollPattern::findNote(int noteId) noexcept {
    for (auto& n : m_notes) {
        if (n.id == noteId) return &n;
    }
    return nullptr;
}

void PianoRollPattern::setNotes(std::vector<MidiNote> notes) {
    m_notes = std::move(notes);
    int maxId = 0;
    for (const auto& n : m_notes) maxId = std::max(maxId, n.id);
    std::vector<int> seen;
    for (auto& n : m_notes) {
        if (n.id <= 0 || std::find(seen.begin(), seen.end(), n.id) != seen.end()) n.id = ++maxId;
        seen.push_back(n.id);
        n.note = std::clamp(n.note, 0, 127);
        n.velocity = std::clamp(n.velocity, 0, 127);
        n.lengthBeats = std::max(0.01, n.lengthBeats);
        n.selected = false;
    }
    m_nextId = maxId + 1;
}
