#include "timeline.h"
#include "core/valueformat.h"
#include "transport/musicalgrid.h"

#include <algorithm>
#include <cmath>

void Timeline::setTracks(TrackList* tracks)
{
    m_tracks = tracks;
    deselect(); cancelEdit(); cancelSelection(); m_range = {}; m_pendingRange.reset();
}
void Timeline::setMeter(int numerator, int denominator)
{
    if (numerator > 0 && denominator > 0) { m_numerator = numerator; m_denominator = denominator; }
}
void Timeline::setBpm(double bpm) { if (std::isfinite(bpm) && bpm > 0) m_bpm = bpm; }
void Timeline::setSnapMode(int mode) { if (mode >= 0 && mode <= 6) m_snapMode = mode; }
double Timeline::snapStep() const
{
    if (m_snapMode == 6) return 0;
    return m_snapMode == 0 ? beatsPerBar() : beatUnit() / std::pow(2.0, m_snapMode - 1);
}
double Timeline::snap(double beat, std::string_view direction) const
{
    // Shared grid maths (MusicalGrid). Timeline steps always divide the bar, so the per-bar
    // grid gives the same positions as a plain grid from beat 0.
    MusicalGrid grid;
    grid.setMeter(beatsPerBar(), beatUnit());
    grid.setStep(snapStep());
    const auto rounding = direction == "floor" ? MusicalGrid::Rounding::Floor
        : direction == "ceil" ? MusicalGrid::Rounding::Ceil : MusicalGrid::Rounding::Nearest;
    return grid.snap(beat, rounding);
}
double Timeline::contentEndBars() const { return m_tracks ? m_tracks->project().contentEndBeats() / beatsPerBar() : 0; }
double Timeline::clipEndBeat(int id, int clipId) const
{
    const auto* clip = m_tracks ? m_tracks->findClip(id, clipId) : nullptr;
    return clip ? m_tracks->project().tempo().beatAt(clip->start + clip->length) : 0;
}
bool Timeline::isBarLine(double beat) const
{
    return isBarLine(beat, beatsPerBar());
}
bool Timeline::isBarLine(double beat, double beatsPerBar)
{
    return MusicalGrid::isOnGrid(beat, beatsPerBar, 1e-7);
}
std::string Timeline::barLabel(double bar) const { return valueformat::number(bar + 1); }
bool Timeline::hasOverlap(int id, double start, double length) const
{
    if (!m_tracks) return false;
    for (const auto& clip : m_tracks->clips())
        if (clip.trackId == id && start < clip.startBeat + clip.lengthBeats && start + length > clip.startBeat) return true;
    return false;
}
std::optional<Timeline::CreationRequest> Timeline::createClip(double beat, int lane) const
{
    if (!m_tracks || m_busy || m_loading || m_recording || m_tracks->editsLocked() || !std::isfinite(beat)) return std::nullopt;
    const int id = m_tracks->trackIdAt(lane);
    const double start = snap(beat, "floor");
    if (id < 0 || hasOverlap(id, start, beatsPerBar())) return std::nullopt;
    return CreationRequest{id, start, beatsPerBar()};
}
bool Timeline::canSplit(int id, int clipId, double beat) const
{
    if (!m_tracks || m_recording || m_busy || m_loading || m_tracks->editsLocked() || !std::isfinite(beat)) return false;
    const auto* clip = m_tracks->findClip(id, clipId);
    if (!clip) return false;
    const auto& tempo = m_tracks->project().tempo();
    return beat > tempo.beatAt(clip->start) && beat < tempo.beatAt(clip->start + clip->length);
}
bool Timeline::selected(int id) const { return std::find(m_selectedIds.begin(), m_selectedIds.end(), id) != m_selectedIds.end(); }
bool Timeline::keyboardAllowed() const { return m_tracks && !m_selecting && m_operation.empty() && !m_loading; }
bool Timeline::canEditSelection() const { return keyboardAllowed() && !m_busy && !m_tracks->editsLocked() && !m_selectedIds.empty(); }
bool Timeline::canMergeSelection() const { return m_tracks && !m_busy && !m_recording && m_tracks->canMergeSelectedClips(m_selectedIds); }
bool Timeline::canSplitSelection() const
{
    return canEditSelection() && !m_recording && m_selectedIds.size() == 1 && m_selectedClipId >= 0;
}
bool Timeline::canCopySelection() const
{
    return keyboardAllowed() && !m_recording && !m_busy && !m_selectedIds.empty();
}
bool Timeline::canCutSelection() const
{
    return canCopySelection() && canEditSelection();
}
bool Timeline::canPaste() const
{
    return keyboardAllowed() && !m_recording && !m_busy && m_tracks && !m_tracks->editsLocked() && m_tracks->hasClipboard();
}
bool Timeline::canDuplicateSelection() const
{
    return canCopySelection() && canEditSelection();
}
bool Timeline::copySelectedClips()
{
    if (!canCopySelection()) return false;
    return m_tracks->copyClips(m_selectedIds);
}
std::vector<int> Timeline::cutSelectedClips()
{
    if (!canCutSelection()) return {};
    const auto cut = m_tracks->cutClips(m_selectedIds);
    if (!cut.empty()) {
        deselect();
        sync();
    }
    return cut;
}
std::vector<int> Timeline::pasteClips(double targetBeat, int targetTrackId)
{
    if (!canPaste()) return {};
    if (targetTrackId < 0) {
        targetTrackId = m_selectedTrackId >= 0 ? m_selectedTrackId : (m_tracks->count() > 0 ? m_tracks->trackIdAt(0) : -1);
    }
    if (targetBeat < 0.0) targetBeat = 0.0;
    const auto pasted = m_tracks->pasteClips(targetTrackId, targetBeat);
    if (!pasted.empty()) {
        m_selectedIds = pasted;
        m_selectedClipId = pasted.front();
        m_selectedTrackId = targetTrackId;
        sync();
    }
    return pasted;
}
std::vector<int> Timeline::duplicateSelectedClips(double beatOffset)
{
    if (!canDuplicateSelection()) return {};
    const auto duplicated = m_tracks->duplicateClips(m_selectedIds, beatOffset);
    if (!duplicated.empty()) {
        m_selectedIds = duplicated;
        m_selectedClipId = duplicated.front();
        sync();
    }
    return duplicated;
}
int Timeline::selectedTrackId() const
{
    return m_tracks ? m_tracks->selectedTrackId() : m_selectedTrackId;
}
bool Timeline::selectTrack(int id)
{
    if (m_tracks) return m_tracks->selectTrack(id);
    m_selectedTrackId = id;
    return true;
}
void Timeline::deselect()
{
    m_selectedIds.clear(); m_selectedClipId = -1; m_hasSelection = false;
}
void Timeline::selectAll()
{
    deselect();
    if (!m_tracks) return;
    const auto clips = m_tracks->clips();
    for (const auto& clip : clips) m_selectedIds.push_back(clip.clipId);
    if (!clips.empty()) { m_selectedClipId = clips.front().clipId; selectTrack(clips.front().trackId); }
}
void Timeline::selectClip(int id, int clipId, bool toggle, bool preserve)
{
    if (!m_tracks || !m_tracks->findClip(id, clipId)) return;
    const auto at = std::find(m_selectedIds.begin(), m_selectedIds.end(), clipId);
    if (toggle) {
        if (at == m_selectedIds.end()) m_selectedIds.push_back(clipId);
        else m_selectedIds.erase(at);
    } else if (!preserve || at == m_selectedIds.end()) m_selectedIds = {clipId};
    m_selectedClipId = selected(clipId) ? clipId : -1;
    if (m_selectedClipId >= 0) selectTrack(id);
    m_hasSelection = false;
}
void Timeline::sync()
{
    if (!m_tracks) { deselect(); return; }
    const auto clips = m_tracks->clips();
    std::erase_if(m_selectedIds, [&](int id) { return std::none_of(clips.begin(), clips.end(), [id](const auto& c) { return c.clipId == id; }); });
    auto primary = std::find_if(clips.begin(), clips.end(), [&](const auto& c) { return c.clipId == m_selectedClipId; });
    if (primary == clips.end()) primary = std::find_if(clips.begin(), clips.end(), [&](const auto& c) { return selected(c.clipId); });
    m_selectedClipId = primary == clips.end() ? -1 : primary->clipId;
    if (primary != clips.end()) selectTrack(primary->trackId);
    if (m_selectedIds.empty()) cancelEdit();
}
void Timeline::beginEdit(const std::string& operation)
{
    if (!canEditSelection() || (operation != "move" && operation != "trimStart" && operation != "resize")) return;
    m_operation = operation; m_delta = 0; m_lanes = 0;
}
void Timeline::updateEdit(double delta)
{
    if (!m_tracks || m_operation.empty() || !std::isfinite(delta)) return;
    double lower = -INFINITY, upper = INFINITY;
    for (const auto& clip : m_tracks->clips()) if (selected(clip.clipId)) {
        if (m_operation == "move" || m_operation == "trimStart") lower = std::max(lower, -clip.startBeat);
        if (m_operation == "resize") lower = std::max(lower, 1e-6 - clip.lengthBeats);
        if (m_operation == "trimStart") upper = std::min(upper, clip.lengthBeats - 1e-6);
    }
    // A one-sample clip at a very low tempo can be shorter than the visual edit epsilon.
    m_delta = lower <= upper ? std::clamp(delta, lower, upper) : 0;
}
void Timeline::updateLane(int delta)
{
    if (!m_tracks || m_operation != "move") return;
    int lower = -m_tracks->count(), upper = m_tracks->count() - 1;
    for (const auto& clip : m_tracks->clips()) if (selected(clip.clipId)) {
        lower = std::max(lower, -clip.laneIndex);
        upper = std::min(upper, m_tracks->count() - 1 - clip.laneIndex);
    }
    m_lanes = lower <= upper ? std::clamp(delta, lower, upper) : 0;
}
void Timeline::updateDrag(double originalBeat, double deltaBeats, bool verticalOnly)
{
    updateEdit(verticalOnly ? 0 : snap(originalBeat + deltaBeats) - originalBeat);
}
void Timeline::cancelEdit() { m_operation.clear(); m_delta = 0; m_lanes = 0; }
std::optional<Timeline::EditRequest> Timeline::commitEdit()
{
    if (m_operation.empty()) return std::nullopt;
    if (m_busy || m_loading || m_recording) { cancelEdit(); return std::nullopt; }
    EditRequest request{m_selectedIds, m_operation, m_delta, m_operation == "move" ? m_lanes : 0};
    cancelEdit();
    return request;
}
std::optional<Timeline::EditRequest> Timeline::nudge(int direction) const
{
    if (!canEditSelection()) return std::nullopt;
    return EditRequest{m_selectedIds, "move", direction * (snapStep() > 0 ? snapStep() : m_bpm / 60000), 0};
}
std::vector<Timeline::Preview> Timeline::previews() const
{
    std::vector<Preview> result;
    if (m_tracks) for (const auto& clip : m_tracks->clips()) {
        Preview view{clip.clipId, clip.laneIndex, clip.startBeat, clip.lengthBeats, selected(clip.clipId)};
        if (view.selected && !m_operation.empty()) {
            if (m_operation == "move" || m_operation == "trimStart") view.start += m_delta;
            if (m_operation == "move") view.lane += m_lanes;
            if (m_operation == "resize") view.length += m_delta;
            if (m_operation == "trimStart") view.length -= m_delta;
        }
        result.push_back(view);
    }
    return result;
}
void Timeline::beginSelection(double beat, double lane, bool clips)
{
    if (!std::isfinite(beat) || !std::isfinite(lane)) return;
    deselect(); cancelEdit();
    m_selecting = m_hasSelection = true; m_clipSelection = clips; m_dragged = false;
    m_startBeat = m_endBeat = beat; m_startLane = m_endLane = lane;
}
void Timeline::updateSelection(double beat, double lane, bool dragged)
{
    if (!m_selecting || !std::isfinite(beat) || !std::isfinite(lane)) return;
    m_endBeat = beat; m_endLane = lane; m_dragged = dragged;
}
double Timeline::rangeStart() const
{
    if (!m_selecting || m_clipSelection) return m_range.start;
    const double start = std::max(0.0, std::min(m_startBeat, m_endBeat));
    return m_snapSelection ? snap(start, "floor") : start;
}
double Timeline::rangeEnd() const
{
    if (!m_selecting || m_clipSelection || !m_dragged) return m_range.end;
    const double end = std::max(0.0, std::max(m_startBeat, m_endBeat));
    return m_snapSelection ? snap(end, "ceil") : end;
}
double Timeline::playheadBeat(double beat) const
{
    if (!std::isfinite(beat)) return 0.0;
    beat = std::max(0.0, beat);
    return m_snapPlayhead ? std::max(0.0, snap(beat)) : beat;
}
std::optional<Timeline::RepeatRange> Timeline::finishSelection(double topInset, double bottomInset, double padding)
{
    if (!m_selecting) return std::nullopt;
    if (!m_clipSelection) {
        const double start = rangeStart(), end = m_dragged ? rangeEnd() : 0;
        m_selecting = false;
        if (m_recording || m_busy || m_loading) return std::nullopt;
        m_pendingRange = RepeatRange{end > start ? start : 0, end};
        return m_pendingRange;
    }
    m_selecting = false;
    if (m_tracks && m_endBeat != m_startBeat) {
        const double left = std::min(m_startBeat, m_endBeat), right = std::max(m_startBeat, m_endBeat);
        const double top = std::min(m_startLane, m_endLane), bottom = std::max(m_startLane, m_endLane) + padding;
        for (const auto& clip : m_tracks->clips())
            if (clip.startBeat < right && clip.startBeat + clip.lengthBeats > left
                && clip.laneIndex + topInset < bottom && clip.laneIndex + 1 - bottomInset > top)
                m_selectedIds.push_back(clip.clipId);
    }
    return std::nullopt;
}
void Timeline::acceptRepeatRange(bool accepted) { if (accepted && m_pendingRange) m_range = *m_pendingRange; m_pendingRange.reset(); }
void Timeline::cancelSelection() { m_selecting = m_hasSelection = false; }
void Timeline::setRangeStart(double value) { if (std::isfinite(value) && value >= 0) m_range.start = value; }
void Timeline::setRangeEnd(double value) { if (std::isfinite(value) && value >= 0) m_range.end = value; }
