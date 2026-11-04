#include "project.h"
#include "instruments/instrumentcatalog.h"

#include <algorithm>
#include <cmath>
#include <limits>

bool Track::hasLiveInput() const
{
    if (kind == TrackKind::Audio) return true;
    const auto* definition = instrumentcatalog::find(type);
    return definition && definition->hasLiveInput;
}

Track& Project::addTrack(TrackKind kind, const std::string& name, const std::string& type)
{
    auto track = std::make_unique<Track>();
    track->id = m_nextId++;
    static constexpr const char* colours[]{"#58b7b1", "#6fa8dc", "#b18bd9", "#de849c", "#e2a565", "#d8c16b", "#86bd83", "#9babb7"};
    track->colour = colours[(track->id - 1) % std::size(colours)];
    track->kind = kind;
    track->name = name;
    track->type = type.empty() ? (kind == TrackKind::Instrument ? "Instrument" : "Audio") : type;
    track->recordDry = track->type == "Audio";
    m_tracks.push_back(std::move(track));
    return *m_tracks.back();
}

void Project::removeTrack(int id)
{
    const auto it = std::find_if(m_tracks.begin(), m_tracks.end(), [id](const auto& track) {
        return track->id == id;
    });
    if (it != m_tracks.end()) {
        m_tracks.erase(it);
        clearHistory(); // Track deletion itself is not a clip edit.
    }
}

void Project::moveTrack(int from, int to)
{
    if (from < 0 || to < 0 || from >= static_cast<int>(m_tracks.size())
        || to >= static_cast<int>(m_tracks.size()) || from == to)
        return;

    auto track = std::move(m_tracks[static_cast<std::size_t>(from)]);
    m_tracks.erase(m_tracks.begin() + from);
    m_tracks.insert(m_tracks.begin() + to, std::move(track));
}

Track* Project::findTrack(int id)
{
    const auto it = std::find_if(m_tracks.begin(), m_tracks.end(), [id](const auto& track) {
        return track->id == id;
    });
    return it == m_tracks.end() ? nullptr : it->get();
}

const Track* Project::findTrack(int id) const
{
    const auto it = std::find_if(m_tracks.begin(), m_tracks.end(), [id](const auto& track) {
        return track->id == id;
    });
    return it == m_tracks.end() ? nullptr : it->get();
}

bool Project::setTrackName(int id, const std::string& name)
{
    auto* track = findTrack(id);
    if (!track || name.empty() || track->name == name) return false;
    track->name = name;
    return true;
}

bool Project::setTrackColour(int id, const std::string& colour)
{
    auto* track = findTrack(id);
    if (!track || colour.size() != 7 || colour.front() != '#'
        || !std::all_of(colour.begin() + 1, colour.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        })) return false;
    track->colour = colour;
    for (char& c : track->colour) if (c >= 'A' && c <= 'F') c += 'a' - 'A';
    return true;
}

bool Project::setTrackSolo(int id, bool enabled)
{
    auto* track = findTrack(id);
    if (!track)
        return false;
    track->solo = enabled;
    return true;
}

bool Project::setTrackMuted(int id, bool enabled)
{
    auto* track = findTrack(id);
    if (!track)
        return false;
    track->muted = enabled;
    return true;
}

bool Project::setMasterVolume(float volume) noexcept
{
    if (!std::isfinite(volume)) return false;
    const float clamped = std::clamp(volume, 0.0f, 2.0f);
    if (m_masterVolume == clamped) return false;
    m_masterVolume = clamped;
    return true;
}

bool Project::setMasterMuted(bool muted) noexcept
{
    if (m_masterMuted == muted) return false;
    m_masterMuted = muted;
    return true;
}

bool Project::setTrackRecordArmed(int id, bool enabled)
{
    auto* track = findTrack(id);
    if (!track)
        return false;
    track->recordArmed = enabled;
    return true;
}

bool Project::setTrackInputPort(int id, const std::string& port)
{
    auto* track = findTrack(id);
    if (!track)
        return false;
    track->inputPort = port;
    return true;
}

bool Project::setTrackVolume(int id, float volume)
{
    auto* track = findTrack(id);
    if (!track || !std::isfinite(volume))
        return false;
    track->volume = std::clamp(volume, 0.0f, 2.0f);
    return true;
}

bool Project::setTrackEqBand(int id, int band, float gainDb)
{
    auto* track = findTrack(id);
    if (!track || band < 0 || band >= static_cast<int>(track->eqGainDb.size()) || !std::isfinite(gainDb))
        return false;
    track->eqGainDb[static_cast<std::size_t>(band)] = std::clamp(gainDb, -12.0f, 12.0f);
    return true;
}

int Project::addAudioClip(int trackId, AudioClip clip)
{
    auto* track = findTrack(trackId);
    if (!track || clip.start < 0 || clip.length <= 0 || clip.sourceOffset < 0
        || clip.start > std::numeric_limits<std::int64_t>::max() - clip.length)
        return -1;
    clip.id = m_nextClipId++;
    auto before = track->audioClips;
    track->audioClips.push_back(std::move(clip));
    rememberClipEdit(trackId, std::move(before));
    return track->audioClips.back().id;
}

std::vector<int> Project::addAudioClips(const std::vector<std::pair<int, AudioClip>>& clips)
{
    if (clips.empty()) return {};
    for (const auto& [trackId, clip] : clips) {
        const auto* track = findTrack(trackId);
        if (!track || clip.start < 0 || clip.length <= 0 || clip.sourceOffset < 0
            || clip.start > std::numeric_limits<std::int64_t>::max() - clip.length)
            return {};
    }
    ClipEdit edit;
    std::vector<int> newIds;
    newIds.reserve(clips.size());
    for (const auto& [trackId, clipCandidate] : clips) {
        if (std::none_of(edit.tracks.begin(), edit.tracks.end(), [trackId](const auto& t) { return t.trackId == trackId; })) {
            const auto& existingClips = findTrack(trackId)->audioClips;
            edit.tracks.push_back({trackId, existingClips, existingClips});
        }
        auto trackEdit = std::find_if(edit.tracks.begin(), edit.tracks.end(), [trackId](const auto& t) { return t.trackId == trackId; });
        AudioClip clip = clipCandidate;
        clip.id = m_nextClipId++;
        newIds.push_back(clip.id);
        trackEdit->after.push_back(std::move(clip));
    }
    m_history.erase(m_history.begin() + m_historyPosition, m_history.end());
    m_history.push_back(std::move(edit));
    if (m_history.size() > 100) m_history.erase(m_history.begin());
    m_historyPosition = m_history.size();
    for (const auto& state : m_history.back().tracks) findTrack(state.trackId)->audioClips = state.after;
    return newIds;
}

bool Project::removeAudioClip(int trackId, int clipId)
{
    auto* track = findTrack(trackId);
    if (!track)
        return false;
    const auto it = std::find_if(track->audioClips.begin(), track->audioClips.end(), [clipId](const AudioClip& clip) {
        return clip.id == clipId;
    });
    if (it == track->audioClips.end())
        return false;
    auto before = track->audioClips;
    track->audioClips.erase(it);
    rememberClipEdit(trackId, std::move(before));
    return true;
}

bool Project::resizeAudioClip(int trackId, int clipId, std::int64_t lengthSamples)
{
    auto* track = findTrack(trackId);
    if (!track || lengthSamples <= 0)
        return false;
    const auto it = std::find_if(track->audioClips.begin(), track->audioClips.end(), [clipId](const AudioClip& clip) {
        return clip.id == clipId;
    });
    if (it == track->audioClips.end()
        || it->start > std::numeric_limits<std::int64_t>::max() - lengthSamples)
        return false;
    if (it->length == lengthSamples) return true;
    auto before = track->audioClips;
    it->length = lengthSamples;
    rememberClipEdit(trackId, std::move(before));
    return true;
}

bool Project::setClipBounds(int trackId, int clipId, std::int64_t start, std::int64_t length, std::int64_t offset) {
    auto* track = findTrack(trackId);
    if (!track || start < 0 || length <= 0 || offset < 0 || start > INT64_MAX - length) return false;
    auto clip = std::find_if(track->audioClips.begin(), track->audioClips.end(), [clipId](const auto& c){return c.id==clipId;});
    if (clip == track->audioClips.end()) return false;
    if (clip->start == start && clip->length == length && clip->sourceOffset == offset) return true;
    auto before = track->audioClips;
    clip->start=start; clip->length=length; clip->sourceOffset=offset;
    rememberClipEdit(trackId,std::move(before)); return true;
}
int Project::splitAudioClip(int trackId, int clipId, std::int64_t position, std::int64_t sourceOffset) {
    auto* track = findTrack(trackId); if (!track || sourceOffset < 0) return -1;
    auto clip = std::find_if(track->audioClips.begin(), track->audioClips.end(), [clipId](const auto& c){return c.id==clipId;});
    if (clip == track->audioClips.end() || position <= clip->start || position >= clip->start+clip->length) return -1;
    auto before = track->audioClips; AudioClip right=*clip;
    right.id=m_nextClipId++; right.start=position; right.length=clip->start+clip->length-position; right.sourceOffset=sourceOffset;
    clip->length=position-clip->start;
    if (!clip->notes.empty()) {
        // Notes are origin-relative, so the split point in note space is the right half's
        // offset. Notes starting before it stay left, the rest move right (no duplicates).
        const double splitBeat=double(sourceOffset)*m_tempo.bpm()/(60.0*m_tempo.sampleRate());
        right.notes.clear();
        std::vector<MidiNote> left;
        for (const auto& n:clip->notes) (n.startBeat<splitBeat ? left : right.notes).push_back(n);
        clip->notes=std::move(left);
    }
    track->audioClips.push_back(right);
    rememberClipEdit(trackId,std::move(before)); return right.id;
}

bool Project::canMergeAudioClips(int trackId, int first, int second, double sourceRate) const {
    const auto* track=findTrack(trackId);
    if (!track || first==second || !std::isfinite(sourceRate) || sourceRate<=0) return false;
    const AudioClip *a=nullptr,*b=nullptr;
    for (const auto& clip:track->audioClips) {
        if (clip.id==first) a=&clip;
        if (clip.id==second) b=&clip;
    }
    if (!a || !b) return false;
    if (a->start>b->start) std::swap(a,b);
    return a->start+a->length==b->start && a->poolId==b->poolId
        && a->sourceOffset+std::llround(a->length*sourceRate/m_tempo.sampleRate())==b->sourceOffset
        && a->effectsPrinted==b->effectsPrinted && a->gainDb==b->gainDb
        && a->fadeOut==0 && b->fadeIn==0;
}

bool Project::mergeAudioClips(int trackId, int first, int second, double sourceRate) {
    if (!canMergeAudioClips(trackId,first,second,sourceRate)) return false;
    auto* track=findTrack(trackId);
    auto a=std::find_if(track->audioClips.begin(),track->audioClips.end(),[&](const auto& c){return c.id==first;});
    auto b=std::find_if(track->audioClips.begin(),track->audioClips.end(),[&](const auto& c){return c.id==second;});
    if (a->start>b->start) std::swap(a,b);
    auto before=track->audioClips;
    a->length+=b->length; a->fadeOut=b->fadeOut;
    track->audioClips.erase(b);
    rememberClipEdit(trackId,std::move(before)); return true;
}

bool Project::moveAudioClip(int trackId, int clipId, std::int64_t start) {
    auto* track = findTrack(trackId);
    if (!track || start < 0) return false;
    auto clip = std::find_if(track->audioClips.begin(), track->audioClips.end(),
        [clipId](const auto& c) { return c.id == clipId; });
    if (clip == track->audioClips.end() || start > INT64_MAX - clip->length) return false;
    if (clip->start == start) return true;
    auto before = track->audioClips; clip->start = start;
    rememberClipEdit(trackId, std::move(before)); return true;
}

bool Project::setClipGain(int trackId, int clipId, float gainDb) {
    auto* track = findTrack(trackId);
    if (!track || !std::isfinite(gainDb)) return false;
    const float clampedGain = std::clamp(gainDb, -120.0f, 24.0f);
    auto it = std::find_if(track->audioClips.begin(), track->audioClips.end(),
                           [clipId](const auto& c) { return c.id == clipId; });
    if (it == track->audioClips.end()) return false;
    if (it->gainDb == clampedGain) return true;
    auto before = track->audioClips;
    it->gainDb = clampedGain;
    rememberClipEdit(trackId, std::move(before));
    return true;
}

bool Project::setClipNotes(int trackId, int clipId, std::vector<MidiNote> notes) {
    auto* track = findTrack(trackId);
    if (!track) return false;
    auto it = std::find_if(track->audioClips.begin(), track->audioClips.end(),
                           [clipId](const auto& c) { return c.id == clipId; });
    if (it == track->audioClips.end()) return false;
    for (auto& n : notes) n.selected = false; // selection is editor state, not document state
    if (it->notes == notes) return true;
    auto before = track->audioClips;
    it->notes = std::move(notes);
    rememberClipEdit(trackId, std::move(before));
    return true;
}

bool Project::editClipGroup(const std::vector<ClipChange>& changes) {
    if (changes.empty()) return false;
    ClipEdit edit;
    std::vector<int> ids;
    for (const auto& change:changes) {
        const auto* track=findTrack(change.trackId);
        const auto& clip=change.clip;
        if (!track || std::find(ids.begin(),ids.end(),clip.id)!=ids.end()
            || std::none_of(track->audioClips.begin(),track->audioClips.end(),[&](const auto& c){return c.id==clip.id;})
            || clip.start<0 || clip.length<=0 || clip.sourceOffset<0 || clip.start>INT64_MAX-clip.length) return false;
        ids.push_back(clip.id);
        const int destinationId=change.remove || change.destinationTrackId<0 ? change.trackId : change.destinationTrackId;
        if (!findTrack(destinationId)) return false;
        for (int id:{change.trackId,destinationId}) {
            if (std::none_of(edit.tracks.begin(),edit.tracks.end(),[&](const auto& t){return t.trackId==id;})) {
                const auto& clips=findTrack(id)->audioClips;
                edit.tracks.push_back({id,clips,clips});
            }
        }
        auto source=std::find_if(edit.tracks.begin(),edit.tracks.end(),[&](const auto& t){return t.trackId==change.trackId;});
        auto at=std::find_if(source->after.begin(),source->after.end(),[&](const auto& c){return c.id==clip.id;});
        if (change.remove || destinationId!=change.trackId) source->after.erase(at);
        else *at=clip;
        if (!change.remove && destinationId!=change.trackId) {
            auto destination=std::find_if(edit.tracks.begin(),edit.tracks.end(),[&](const auto& t){return t.trackId==destinationId;});
            destination->after.push_back(clip);
        }
    }
    m_history.erase(m_history.begin()+m_historyPosition,m_history.end());
    m_history.push_back(std::move(edit));
    if (m_history.size()>100) m_history.erase(m_history.begin());
    m_historyPosition=m_history.size();
    for (const auto& state:m_history.back().tracks) findTrack(state.trackId)->audioClips=state.after;
    return true;
}

void Project::rememberClipEdit(int trackId, std::vector<AudioClip> before)
{
    m_history.erase(m_history.begin() + m_historyPosition, m_history.end());
    m_history.push_back({{{trackId, std::move(before), findTrack(trackId)->audioClips}}});
    if (m_history.size() > 100) m_history.erase(m_history.begin());
    m_historyPosition = m_history.size();
}

int Project::undo()
{
    if (!canUndo()) return -1;
    const auto& edit = m_history[--m_historyPosition];
    for (const auto& track:edit.tracks) findTrack(track.trackId)->audioClips = track.before;
    return edit.tracks.front().trackId;
}

int Project::redo()
{
    if (!canRedo()) return -1;
    const auto& edit = m_history[m_historyPosition++];
    for (const auto& track:edit.tracks) findTrack(track.trackId)->audioClips = track.after;
    return edit.tracks.front().trackId;
}

double Project::contentEndBeats() const noexcept
{
    double endBeat = 0.0;
    for (const auto& track : m_tracks) {
        for (const auto& clip : track->audioClips) {
            if (clip.start < 0 || clip.length <= 0
                || clip.start > std::numeric_limits<std::int64_t>::max() - clip.length)
                continue;
            endBeat = std::max(endBeat, m_tempo.beatAt(clip.start + clip.length));
        }
    }
    return endBeat;
}
