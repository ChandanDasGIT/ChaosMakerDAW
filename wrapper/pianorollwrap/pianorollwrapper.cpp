#include "pianorollwrapper.h"

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

int PianoRollWrapper::addNote(int note, double startBeat, double lengthBeats, int velocity) {
    int id = m_pattern.addNote(note, startBeat, lengthBeats, velocity);
    emit notesChanged();
    return id;
}

bool PianoRollWrapper::removeNote(int noteId) {
    if (m_pattern.removeNote(noteId)) {
        emit notesChanged();
        return true;
    }
    return false;
}

bool PianoRollWrapper::removeNotesAt(int note, double beat) {
    if (m_pattern.removeNotesAt(note, beat)) {
        emit notesChanged();
        return true;
    }
    return false;
}

bool PianoRollWrapper::moveNote(int noteId, int newNote, double newStartBeat) {
    if (m_pattern.moveNote(noteId, newNote, newStartBeat)) {
        emit notesChanged();
        return true;
    }
    return false;
}

bool PianoRollWrapper::resizeNote(int noteId, double newLengthBeats) {
    if (m_pattern.resizeNote(noteId, newLengthBeats)) {
        emit notesChanged();
        return true;
    }
    return false;
}

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

bool PianoRollWrapper::deleteSelected() {
    if (m_pattern.deleteSelected()) {
        emit notesChanged();
        return true;
    }
    return false;
}

void PianoRollWrapper::clear() {
    m_pattern.clear();
    emit notesChanged();
}
