#include "pianoRollWrap.h"

#include <algorithm>
#include <cmath>

#include "common/core/valueformat.h"
#include "common/instruments/instrumentcatalog.h"
#include "common/transport/tempomap.h"
#include "pianoRoll/notedrag.h"

namespace {
std::vector<MidiNote> withoutSelection(std::vector<MidiNote> notes)
{
    for (auto& n : notes) n.selected = false;
    return notes;
}
}

PianoRollWrap::PianoRollWrap(QObject* parent)
    : QObject(parent) {
    // Every edit path announces notesChanged; the model follows from there.
    connect(this, &PianoRollWrap::notesChanged, this, [this] { m_noteModel.sync(m_pattern.notes()); });
}

QVariantList PianoRollWrap::notes() const {
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

void PianoRollWrap::setTrackList(TrackListModelWrap* trackList) {
    if (m_trackList == trackList) return;
    if (m_clipsConnection) disconnect(m_clipsConnection);
    m_trackList = trackList;
    if (trackList)
        m_clipsConnection = connect(trackList, &TrackListModelWrap::clipsChanged, this, &PianoRollWrap::reload);
    bindingEdited();
}

void PianoRollWrap::setTrackId(int id) {
    if (m_trackId == id) return;
    m_trackId = id;
    bindingEdited();
}

void PianoRollWrap::setClipId(int id) {
    if (m_clipId == id) return;
    m_clipId = id;
    bindingEdited();
}

bool PianoRollWrap::bound() const {
    return m_trackList && m_trackList->domain().findClip(m_trackId, m_clipId);
}

double PianoRollWrap::clipStartBeat() const {
    return bound() ? m_trackList->domain().clipStartBeat(m_trackId, m_clipId) : 0.0;
}

double PianoRollWrap::clipLengthBeats() const {
    return bound() ? m_trackList->domain().clipLengthBeats(m_trackId, m_clipId) : 0.0;
}

double PianoRollWrap::beatsPerBar() const {
    return m_trackList ? m_trackList->domain().project().tempo().beatsPerBar() : TempoMap{}.beatsPerBar();
}

double PianoRollWrap::beatUnit() const {
    return m_trackList ? m_trackList->domain().project().tempo().beatUnit() : TempoMap{}.beatUnit();
}

QString PianoRollWrap::trackColour() const {
    if (!m_trackList) return {};
    const auto* track = m_trackList->domain().project().findTrack(m_trackId);
    return track ? QString::fromStdString(track->colour) : QString();
}

bool PianoRollWrap::drumMode() const {
    if (!m_trackList) return false;
    const auto* track = m_trackList->domain().project().findTrack(m_trackId);
    return track && instrumentcatalog::namesKeys(track->type);
}

QString PianoRollWrap::drumKit() const {
    if (!drumMode()) return {};
    return QString::fromStdString(m_trackList->domain().project().findTrack(m_trackId)->type);
}

QString PianoRollWrap::drumName(int note) const {
    if (!m_trackList) return {};
    const auto* track = m_trackList->domain().project().findTrack(m_trackId);
    if (!track) return {};
    if (track->type == "Chords") {                         // the strip a key plays: "3 · F", "Bass 3 · F", "3 high · F"
        switch (ChordPlayer::zoneOfKey(note)) {
        case ChordPlayer::Low: return QStringLiteral("Zone low");
        case ChordPlayer::Middle: return QStringLiteral("Zone middle");
        case ChordPlayer::High: return QStringLiteral("Zone high");
        default: break;
        }
        bool bass = false;
        int zone = -1;
        const int strip = ChordPlayer::stripOfKey(note, bass, zone);
        if (strip < 0) return {};
        const auto& chord = track->chords.chords[std::size_t(strip)];
        QString label = (bass ? QStringLiteral("Bass %1") : QStringLiteral("%1")).arg(strip + 1);
        if (zone == ChordPlayer::Low) label += QStringLiteral(" low");
        if (zone == ChordPlayer::High) label += QStringLiteral(" high");
        return chord.empty() ? label : label + QStringLiteral(" \u00b7 ") + QString::fromStdString(chord);
    }
    const auto name = instrumentcatalog::drumName(track->type, note);
    return QString::fromUtf8(name.data(), qsizetype(name.size()));
}

namespace {
QStringList toQStringList(const std::vector<std::string>& names)
{
    QStringList result;
    for (const auto& name : names) result.push_back(QString::fromStdString(name));
    return result;
}
}

QStringList PianoRollWrap::gridDivisionOptions() { return toQStringList(PianoRollGrid::divisionNames()); }
QStringList PianoRollWrap::gridStyleOptions() { return toQStringList(PianoRollGrid::styleNames()); }
QStringList PianoRollWrap::noteLengthOptions() { return toQStringList(PianoRollGrid::noteLengthNames()); }

void PianoRollWrap::setGridDivision(const QString& name) {
    if (name == gridDivision() || !m_grid.setDivision(name.toStdString())) return;
    emit gridChanged();
}

void PianoRollWrap::setGridStyle(const QString& name) {
    if (name == gridStyle() || !m_grid.setStyle(name.toStdString())) return;
    emit gridChanged();
}

void PianoRollWrap::setNoteLengthMode(const QString& name) {
    if (name == noteLengthMode() || !m_grid.setNoteLength(name.toStdString())) return;
    emit gridChanged();
}

void PianoRollWrap::setSnapEnabled(bool enabled) {
    if (enabled == m_grid.snapEnabled()) return;
    m_grid.setSnapEnabled(enabled);
    emit gridChanged();
}

void PianoRollWrap::setPixelsPerBeat(double pixels) {
    if (!std::isfinite(pixels) || pixels <= 0 || pixels == m_pixelsPerBeat) return;
    m_pixelsPerBeat = pixels;
    emit gridChanged();
}

void PianoRollWrap::syncGridMeter() {
    m_grid.setMeter(beatsPerBar(), beatUnit());
    emit gridChanged();
}

QString PianoRollWrap::positionLabel(double beat) const {
    return QString::fromStdString(valueformat::barBeatPosition(beat, beatsPerBar(), beatUnit()));
}

void PianoRollWrap::bindingEdited() {
    // A different clip: start fresh (selection does not carry over between clips).
    m_pattern.setNotes(bound() ? m_trackList->domain().clipNotes(m_trackId, m_clipId) : std::vector<MidiNote>{});
    emit bindingChanged();
    emit clipStartChanged();
    emit meterChanged();
    syncGridMeter();
    emit trackColourChanged();
    emit notesChanged();
}

void PianoRollWrap::reload() {
    if (!m_trackList) return;
    // Clip moves, trims, tempo/meter and track colour changes arrive here without touching the notes.
    emit clipStartChanged();
    emit meterChanged();
    syncGridMeter();
    emit trackColourChanged();
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

bool PianoRollWrap::commit() {
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

int PianoRollWrap::addNote(int note, double startBeat, double lengthBeats, int velocity) {
    int id = m_pattern.addNote(note, startBeat, lengthBeats, velocity);
    if (!commit()) id = -1;
    emit notesChanged();
    return id;
}

bool PianoRollWrap::removeNote(int noteId) {
    if (!m_pattern.removeNote(noteId)) return false;
    const bool ok = commit();
    emit notesChanged();
    return ok;
}

bool PianoRollWrap::removeNotesAt(int note, double beat) {
    if (!m_pattern.removeNotesAt(note, beat)) return false;
    const bool ok = commit();
    emit notesChanged();
    return ok;
}

bool PianoRollWrap::moveNote(int noteId, int newNote, double newStartBeat) {
    if (!m_pattern.moveNote(noteId, newNote, newStartBeat)) return false;
    const bool ok = commit();
    emit notesChanged();
    return ok;
}

bool PianoRollWrap::resizeNote(int noteId, double newLengthBeats) {
    if (!m_pattern.resizeNote(noteId, newLengthBeats)) return false;
    const bool ok = commit();
    emit notesChanged();
    return ok;
}

bool PianoRollWrap::setNoteVelocities(const QVariantList& noteIds, const QVariantList& velocities) {
    if (noteIds.isEmpty() || noteIds.size() != velocities.size()) return false;
    bool changed = false;
    for (qsizetype i = 0; i < noteIds.size(); ++i) {
        bool okId = false, okVel = false;
        const int id = noteIds[i].toInt(&okId);
        const int velocity = qRound(velocities[i].toDouble(&okVel));
        const auto* note = okId ? m_pattern.findNote(id) : nullptr;
        if (!note || !okVel) continue;
        if (note->velocity != std::clamp(velocity, 0, 127)) {
            m_pattern.setNoteVelocity(id, velocity); // clamps 0..127
            changed = true;
        }
    }
    if (!changed) return true;
    const bool ok = commit();
    emit notesChanged();
    return ok;
}

bool PianoRollWrap::deleteSelected() {
    if (!m_pattern.deleteSelected()) return false;
    const bool ok = commit();
    emit notesChanged();
    return ok;
}

bool PianoRollWrap::quantize(double stepBeats) {
    if (!m_pattern.quantize(stepBeats, beatsPerBar())) return false;
    const bool ok = commit();
    emit notesChanged();
    return ok;
}

void PianoRollWrap::clear() {
    m_pattern.clear();
    commit();
    emit notesChanged();
}

// ---- Selection (editor state only, never committed) ----

void PianoRollWrap::selectNote(int noteId, bool addToSelection) {
    m_pattern.selectNote(noteId, addToSelection);
    emit notesChanged();
}

void PianoRollWrap::selectAll() {
    m_pattern.selectAll(true);
    emit notesChanged();
}

int PianoRollWrap::selectNotesInRange(int lowNote, int highNote, double startBeat, double endBeat,
                                         bool addToSelection) {
    const int touched = m_pattern.selectInRange(lowNote, highNote, startBeat, endBeat, addToSelection);
    emit notesChanged();
    return touched;
}

void PianoRollWrap::clearSelection() {
    m_pattern.clearSelection();
    emit notesChanged();
}

double PianoRollWrap::movedStart(double start, double delta) const { return notedrag::movedStart(start, delta); }

int PianoRollWrap::movedPitch(int pitch, int delta) const { return notedrag::movedPitch(pitch, delta); }

double PianoRollWrap::resizedLength(double length, double delta) const
{
    return notedrag::resizedLength(length, delta, minimumNoteLength());
}

double PianoRollWrap::drawnLength(double start, double snappedEnd) const
{
    return notedrag::drawnLength(start, snappedEnd, snapStep(), newNoteLength());
}
