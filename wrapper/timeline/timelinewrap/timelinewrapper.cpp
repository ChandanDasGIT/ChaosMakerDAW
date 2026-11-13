#include "timelinewrapper.h"

namespace {
QVariantList ids(const std::vector<int>& values)
{
    QVariantList result;
    for (int id : values) result.append(id);
    return result;
}
}
void TimelineWrapper::setTrackModel(TrackListWrapper* tracks)
{
    if (m_tracks == tracks) return;
    if (m_tracks) disconnect(m_tracks, nullptr, this, nullptr);
    m_tracks = tracks;
    m_timeline.setTracks(tracks ? &tracks->domain() : nullptr);
    if (tracks) {
        const auto refresh = [this] {
            m_timeline.sync();
            if (m_tracks) m_tracks->setSelectedClipId(m_timeline.selectedClipId());
            emit stateChanged();
        };
        connect(tracks, &TrackListWrapper::clipsChanged, this, refresh);
        connect(tracks, &TrackListWrapper::countChanged, this, refresh);
        connect(tracks, &TrackListWrapper::historyChanged, this, refresh);
        connect(tracks, &TrackListWrapper::clipboardChanged, this, refresh);
        connect(tracks, &TrackListWrapper::selectedTrackChanged, this, [this](int) { emit stateChanged(); });
        connect(tracks, &QAbstractItemModel::modelReset, this, [this] {
            m_timeline.setTracks(m_tracks ? &m_tracks->domain() : nullptr); emit stateChanged();
        });
        connect(tracks, &QObject::destroyed, this, [this] {
            m_tracks = nullptr; m_timeline.setTracks(nullptr); emit trackModelChanged(); emit stateChanged();
        });
    }
    emit trackModelChanged(); emit stateChanged();
}
QVariantList TimelineWrapper::selectedClipIds() const { return ids(m_timeline.selectedIds()); }
QVariantMap TimelineWrapper::previews() const
{
    QVariantMap result;
    for (const auto& view : m_timeline.previews()) result.insert(QString::number(view.clipId), QVariantMap{
        {"startBeat", view.start}, {"lengthBeats", view.length}, {"laneIndex", view.lane}, {"selected", view.selected}});
    return result;
}
void TimelineWrapper::apply(const std::optional<Timeline::EditRequest>& request)
{
    if (request && m_tracks)
        m_tracks->editClips(ids(request->ids), QString::fromStdString(request->operation), request->delta, request->lanes);
    emit stateChanged();
}
void TimelineWrapper::commitGroupEdit() { apply(m_timeline.commitEdit()); }

bool TimelineWrapper::copySelectedClips()
{
    if (!canCopy() || !m_tracks) return false;
    const bool ok = rangeMode() ? m_tracks->copyRange(m_timeline.savedRangeStart(), m_timeline.savedRangeEnd())
                                : m_tracks->copyClips(selectedClipIds());
    emit stateChanged();
    return ok;
}

QVariantList TimelineWrapper::cutSelectedClips()
{
    if (!canCut() || !m_tracks) return {};
    if (rangeMode()) {
        m_tracks->cutRange(m_timeline.savedRangeStart(), m_timeline.savedRangeEnd(), ripple());
        emit stateChanged();
        return {};
    }
    const auto cut = m_tracks->cutClips(selectedClipIds(), ripple());
    if (!cut.isEmpty()) {
        m_timeline.deselect();
        m_timeline.sync();
        emit stateChanged();
    }
    return cut;
}

QVariantList TimelineWrapper::pasteClips(double targetBeat, int targetTrackId)
{
    if (!canPaste() || !m_tracks) return {};
    if (targetTrackId < 0) {
        targetTrackId = m_timeline.selectedTrackId() >= 0 ? m_timeline.selectedTrackId() : (m_tracks->count() > 0 ? m_tracks->trackIdAt(0) : -1);
    }
    if (targetBeat < 0.0) targetBeat = 0.0;
    const auto pasted = m_tracks->pasteClips(targetTrackId, targetBeat, ripple());
    if (!pasted.isEmpty()) {
        // Pasted clips may land on several tracks: select each on its own track.
        for (int i = 0; i < pasted.size(); ++i) {
            const int clip = pasted[i].toInt();
            int track = targetTrackId;
            for (const auto& info : m_tracks->domain().clips()) if (info.clipId == clip) { track = info.trackId; break; }
            m_timeline.selectClip(track, clip, false, i > 0);
        }
        if (m_tracks) m_tracks->setSelectedClipId(m_timeline.selectedClipId());
        emit stateChanged();
    }
    return pasted;
}

QVariantList TimelineWrapper::duplicateSelectedClips(double beatOffset)
{
    if (!canDuplicate() || !m_tracks) return {};
    const auto dup = m_tracks->duplicateClips(selectedClipIds(), beatOffset, ripple());
    if (!dup.isEmpty()) {
        const int selTrack = m_timeline.selectedTrackId() >= 0 ? m_timeline.selectedTrackId() : (m_tracks->count() > 0 ? m_tracks->trackIdAt(0) : -1);
        for (int i = 0; i < dup.size(); ++i) {
            m_timeline.selectClip(selTrack, dup[i].toInt(), false, i > 0);
        }
        if (m_tracks) m_tracks->setSelectedClipId(m_timeline.selectedClipId());
        emit stateChanged();
    }
    return dup;
}
bool TimelineWrapper::deleteSelection()
{
    if (!canDelete() || !m_tracks) return false;
    const bool ok = rangeMode() ? m_tracks->deleteRange(m_timeline.savedRangeStart(), m_timeline.savedRangeEnd(), ripple())
                                : m_tracks->deleteClips(selectedClipIds(), ripple());
    if (ok && !rangeMode()) { m_timeline.deselect(); m_timeline.sync(); }
    emit stateChanged();
    return ok;
}
void TimelineWrapper::nudgeClips(int direction) { apply(m_timeline.nudge(direction)); }
void TimelineWrapper::createClipAt(double beat, int lane)
{
    if (const auto request = m_timeline.createClip(beat, lane); request && m_tracks)
        m_tracks->addClipAtBeat(request->trackId, request->start, request->length);
}
void TimelineWrapper::finishSelection(double top, double bottom, double padding)
{
    const auto range = m_timeline.finishSelection(top, bottom, padding);
    if (range) emit repeatRangeRequested(range->start, range->end);
    if (m_tracks) m_tracks->setSelectedClipId(m_timeline.selectedClipId());
    emit stateChanged();
}
void TimelineWrapper::finishSelectionPixels(double top, double bottom, double padding)
{
    const auto range = m_timeline.finishSelectionPixels(top, bottom, padding);
    if (range) emit repeatRangeRequested(range->start, range->end);
    if (m_tracks) m_tracks->setSelectedClipId(m_timeline.selectedClipId());
    emit stateChanged();
}
