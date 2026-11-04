#include "pianorollwrapper.h"

#include <algorithm>

namespace {
std::vector<MidiNote> withoutSelection(std::vector<MidiNote> notes)
{
    for (auto& n : notes) n.selected = false;
    return notes;
}
}

PianoRollWrapper::PianoRollWrapper(QObject* parent)
    : QObject(parent) {
}

QVariantList PianoRollWrapper::notes() const {
    QVariantList list;
    const auto& src = m_pattern.notes();
    list.reserve(static_cast<qsizetype>(src.size()));
    for (const auto& n : src) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), n.id);
        map.insert(QStringLiteral("note"), n.note);
        map.insert(QStringLiteral("startBeat"), n.startBeat);
        map.insert(QStringLiteral("lengthBeats"), n.lengthBeats);
        map.insert(QStringLiteral("velocity"), n.velocity);
        map.insert(QStringLiteral("selected"), n.selected);
        list.append(map);
    }
    return list;
}

// ---- Clip binding ----

void PianoRollWrapper::setTrackList(TrackListWrapper* trackList) {
    if (m_trackList == trackList) return;
    if (m_clipsConnection) disconnect(m_clipsConnection);
    m_trackList = trackList;
    if (trackList)
        m_clipsConnection = connect(trackList, &TrackListWrapper::clipsChanged, this, &PianoRollWrapper::reload);
    bindingEdited();
}

void PianoRollWrapper::setTrackId(int id) {
    if (m_trackId == id) return;
    m_trackId = id;
    bindingEdited();
}

void PianoRollWrapper::setClipId(int id) {
    if (m_clipId == id) return;
    m_clipId = id;
    bindingEdited();
}

bool PianoRollWrapper::bound() const {
    return m_trackList && m_trackList->domain().findClip(m_trackId, m_clipId);
}

void PianoRollWrapper::bindingEdited() {
    // A different clip: start fresh (selection does not carry over between clips).
    m_pattern.setNotes(bound() ? m_trackList->domain().clipNotes(m_trackId, m_clipId) : std::vector<MidiNote>{});
    emit bindingChanged();
    emit notesChanged();
}

void PianoRollWrapper::reload() {
    if (!m_trackList) return;
    // Unbound while a list is set means the clip is gone (deleted / undone away): show nothing.
    auto fresh = bound() ? m_trackList->domain().clipNotes(m_trackId, m_clipId) : std::vector<MidiNote>{};
    if (withoutSelection(m_pattern.notes()) == fresh) return; // nothing changed (e.g. our own commit)

    // Keep the user's selection for notes that still exist.
    std::vector<int> selected;
    for (const auto& n : m_pattern.notes()) if (n.selected) selected.push_back(n.id);
    m_pattern.setNotes(std::move(fresh));
    for (int id : selected) if (auto* n = m_pattern.findNote(id)) n->selected = true;
    emit bindingChanged(); // bound() may have flipped
    emit notesChanged();
}

bool PianoRollWrapper::commit() {
    if (!m_trackList) return true; // unbound: local editing only
    if (bound() && m_trackList->setClipNotes(m_trackId, m_clipId, withoutSelection(m_pattern.notes())))
        return true;
    // Rejected (recording lock, clip gone): show what the document actually holds.
    std::vector<int> selected;
    for (const auto& n : m_pattern.notes()) if (n.selected) selected.push_back(n.id);
    m_pattern.setNotes(bound() ? m_trackList->domain().clipNotes(m_trackId, m_clipId) : std::vector<MidiNote>{});
    for (int id : selected) if (auto* n = m_pattern.findNote(id)) n->selected = true;
    return false;
}

// ---- Edits (each one is committed to the clip) ----

int PianoRollWrapper::addNote(int note, double startBeat, double lengthBeats, int velocity) {
    int id = m_pattern.addNote(note, startBeat, lengthBeats, velocity);
    if (!commit()) id = -1;
    emit notesChanged();
    return id;
}

bool PianoRollWrapper::removeNote(int noteId) {
    if (!m_pattern.removeNote(noteId)) return false;
    const bool ok = commit();
    emit notesChanged();
    return ok;
}

bool PianoRollWrapper::removeNotesAt(int note, double beat) {
    if (!m_pattern.removeNotesAt(note, beat)) return false;
    const bool ok = commit();
    emit notesChanged();
    return ok;
}

bool PianoRollWrapper::moveNote(int noteId, int newNote, double newStartBeat) {
    if (!m_pattern.moveNote(noteId, newNote, newStartBeat)) return false;
    const bool ok = commit();
    emit notesChanged();
    return ok;
}

bool PianoRollWrapper::resizeNote(int noteId, double newLengthBeats) {
    if (!m_pattern.resizeNote(noteId, newLengthBeats)) return false;
    const bool ok = commit();
    emit notesChanged();
    return ok;
}

bool PianoRollWrapper::deleteSelected() {
    if (!m_pattern.deleteSelected()) return false;
    const bool ok = commit();
    emit notesChanged();
    return ok;
}

void PianoRollWrapper::clear() {
    m_pattern.clear();
    commit();
    emit notesChanged();
}

// ---- Selection (editor state only, never committed) ----

void PianoRollWrapper::selectNote(int noteId, bool addToSelection) {
    m_pattern.selectNote(noteId, addToSelection);
    emit notesChanged();
}

void PianoRollWrapper::selectAll() {
    m_pattern.selectAll(true);
    emit notesChanged();
}

void PianoRollWrapper::clearSelection() {
    m_pattern.clearSelection();
    emit notesChanged();
}
