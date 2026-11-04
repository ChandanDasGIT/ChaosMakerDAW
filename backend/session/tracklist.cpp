#include "tracklist.h"
#include "instruments/instrumentcatalog.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>

namespace {
bool space(char32_t c)
{
    return (c >= 9 && c <= 13) || c == 32 || c == 0x85 || c == 0xa0 || c == 0x1680
        || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029
        || c == 0x202f || c == 0x205f || c == 0x3000;
}
struct Character { char32_t value; std::size_t begin, end; };
std::optional<std::string> cleaned(const std::string& text, bool name)
{
    std::vector<Character> characters;
    for (std::size_t i = 0; i < text.size();) {
        const auto begin = i;
        const auto first = static_cast<unsigned char>(text[i++]);
        char32_t c = first;
        int count = 0;
        if (first >= 0xc2 && first <= 0xdf) { c &= 0x1f; count = 1; }
        else if (first >= 0xe0 && first <= 0xef) { c &= 0xf; count = 2; }
        else if (first >= 0xf0 && first <= 0xf4) { c &= 7; count = 3; }
        else if (first >= 0x80) return std::nullopt;
        for (int n = 0; n < count; ++n) {
            if (i >= text.size()) return std::nullopt;
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return std::nullopt;
            c = (c << 6) | (next & 0x3f);
        }
        if ((count == 1 && c < 0x80) || (count == 2 && c < 0x800)
            || (count == 3 && c < 0x10000) || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
            return std::nullopt;
        characters.push_back({c, begin, i});
    }
    std::size_t first = 0, last = characters.size();
    while (first < last && space(characters[first].value)) ++first;
    while (last > first && space(characters[last - 1].value)) --last;
    if (first == last) return std::nullopt;
    int units = 0;
    for (auto i = first; i < last; ++i) {
        const auto c = characters[i].value;
        units += c > 0xffff ? 2 : 1;
        if (name && (c < 32 || (c >= 0x7f && c <= 0x9f) || units > 256)) return std::nullopt;
    }
    return text.substr(characters[first].begin, characters[last - 1].end - characters[first].begin);
}
TrackList::Change result(bool accepted) { return {accepted, accepted}; }
std::optional<std::int64_t> sampleAtBeat(const TempoMap& tempo, double beat)
{
    const double sample = beat * 60.0 * tempo.sampleRate() / tempo.bpm();
    // Reject the rounded double at 2^63 before calling llround.
    if (!std::isfinite(sample) || sample < 0 || sample >= std::ldexp(1.0, 63)) return std::nullopt;
    return std::llround(sample);
}
}

std::shared_ptr<const AudioData> TrackList::audioFor(const std::string& path) const
{
    const auto found = m_audio.find(path);
    return found == m_audio.end() ? nullptr : found->second;
}

void TrackList::replaceDocument(Project project, AudioMap audio, MediaPaths paths)
{
    m_project = std::move(project); m_audio = std::move(audio); m_mediaPaths = std::move(paths);
    m_recordingTracks.clear();
    m_nextAudioNumber = m_nextVocalNumber = m_nextInstrumentNumber = count() + 1;
    m_nextClipNumber = static_cast<int>(clips().size()) + 1;
    m_instrumentNumbers.clear();
    for (const auto& track : m_project.tracks()) ++m_instrumentNumbers[track->type];
}

void TrackList::adoptDocument(TrackList&& candidate)
{
    *this = std::move(candidate);
    m_recordingTracks.clear();
    m_project.clearHistory();
}

std::vector<TrackList::TrackChoice> TrackList::trackChoices()
{
    return {{"Vocal", "Vocal", false}, {"Instrument", "Instrument", true}};
}

std::vector<TrackList::TrackChoice> TrackList::instrumentChoices()
{
    std::vector<TrackChoice> choices;
    for (const auto& entry : instrumentcatalog::instruments())
        choices.push_back({std::string(entry.id), std::string(entry.name), false});
    return choices;
}

bool TrackList::isInstrumentType(const std::string& type) { return instrumentcatalog::find(type); }
bool TrackList::canAddChoice(const std::string& id) const { return id == "Vocal" || isInstrumentType(id); }
int TrackList::addTrackChoice(const std::string& id) { return id == "Vocal" ? addVocalTrack() : addInstrumentTrackOfType(id); }
int TrackList::addAudioTrack()
{
    const int id = m_project.addTrack(TrackKind::Audio, "Audio " + std::to_string(m_nextAudioNumber++), "Audio").id;
    if (m_selectedTrackId < 0) m_selectedTrackId = id;
    return id;
}
int TrackList::addVocalTrack()
{
    const int id = m_project.addTrack(TrackKind::Audio, "Vocal " + std::to_string(m_nextVocalNumber++), "Vocal").id;
    if (m_selectedTrackId < 0) m_selectedTrackId = id;
    return id;
}
int TrackList::addInstrumentTrack()
{
    const int id = m_project.addTrack(TrackKind::Instrument, "Instrument " + std::to_string(m_nextInstrumentNumber++), "Instrument").id;
    if (m_selectedTrackId < 0) m_selectedTrackId = id;
    return id;
}
int TrackList::addInstrumentTrackOfType(const std::string& type)
{
    if (!isInstrumentType(type)) return -1;
    const int id = m_project.addTrack(TrackKind::Instrument, type + ' ' + std::to_string(++m_instrumentNumbers[type]), type).id;
    if (m_selectedTrackId < 0) m_selectedTrackId = id;
    return id;
}
int TrackList::indexOfTrack(int id) const
{
    for (int i = 0; i < count(); ++i) if (m_project.tracks()[i]->id == id) return i;
    return -1;
}
int TrackList::trackIdAt(int row) const { return row >= 0 && row < count() ? m_project.tracks()[row]->id : -1; }
bool TrackList::selectTrack(int id)
{
    if (id < 0) {
        m_selectedTrackId = -1;
        return true;
    }
    if (!m_project.findTrack(id)) return false;
    m_selectedTrackId = id;
    return true;
}
bool TrackList::selectClip(int trackId, int clipId)
{
    if (clipId < 0) {
        m_selectedClipId = -1;
        return true;
    }
    const auto* clip = findClip(trackId, clipId);
    if (!clip) return false;
    m_selectedClipId = clipId;
    selectTrack(trackId);
    return true;
}
bool TrackList::canRemoveTrack(int id) const { return !trackLocked(id) && m_project.findTrack(id); }
bool TrackList::removeTrack(int id)
{
    if (!canRemoveTrack(id)) return false;
    const int removedRow = indexOfTrack(id);
    m_project.removeTrack(id);
    if (m_selectedTrackId == id) {
        if (count() > 0) {
            const int nextRow = std::min(removedRow, count() - 1);
            m_selectedTrackId = trackIdAt(nextRow);
        } else {
            m_selectedTrackId = -1;
        }
    }
    return true;
}
bool TrackList::canMoveTrack(int id, int row) const { return indexOfTrack(id) >= 0 && row >= 0 && row < count(); }
bool TrackList::moveTrack(int id, int row)
{
    if (!canMoveTrack(id, row)) return false;
    m_project.moveTrack(indexOfTrack(id), row); return true;
}
TrackList::Change TrackList::renameTrack(int id, const std::string& name)
{
    const auto value = cleaned(name, true);
    const auto* track = m_project.findTrack(id);
    if (!track || !value) return {};
    if (track->name == *value) return {true, false};
    return result(m_project.setTrackName(id, *value));
}
TrackList::Change TrackList::setTrackColour(int id, const std::string& colour)
{
    auto value = cleaned(colour, false);
    const auto* track = m_project.findTrack(id);
    if (!track || !value) return {};
    for (auto& c : *value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (track->colour == *value) return {true, false};
    return result(m_project.setTrackColour(id, *value));
}
TrackList::Change TrackList::setSolo(int id, bool value) { return result(m_project.setTrackSolo(id, value)); }
TrackList::Change TrackList::setMuted(int id, bool value) { return result(m_project.setTrackMuted(id, value)); }
TrackList::Change TrackList::setRecordArmed(int id, bool value) { return result(m_project.setTrackRecordArmed(id, value)); }
TrackList::Change TrackList::setVolume(int id, double value) { return result(m_project.setTrackVolume(id, static_cast<float>(value))); }
TrackList::Change TrackList::setMasterVolume(double value)
{
    if (!std::isfinite(value)) return {};
    const float clamped = std::clamp(static_cast<float>(value), 0.0f, 2.0f);
    if (m_project.masterVolume() == clamped) return {true, false};
    return result(m_project.setMasterVolume(clamped));
}
TrackList::Change TrackList::setMasterMuted(bool value)
{
    if (m_project.masterMuted() == value) return {true, false};
    return result(m_project.setMasterMuted(value));
}
TrackList::Change TrackList::setEqBand(int id, int band, double value)
{
    return result(std::isfinite(value) && m_project.setTrackEqBand(id, band, static_cast<float>(value)));
}
TrackList::Change TrackList::setMonitorEnabled(int id, bool value)
{
    auto* track = m_project.findTrack(id);
    if (!track || !track->hasLiveInput()) return {};
    if (track->monitorEnabled == value) return {true, false};
    track->monitorEnabled = value; return {true, true};
}
TrackList::Change TrackList::setRecordDry(int id, bool value)
{
    auto* track = m_project.findTrack(id);
    if (!track || !track->hasLiveInput() || trackLocked(id)) return {};
    if (track->recordDry == value) return {true, false};
    track->recordDry = value; return {true, true};
}
TrackList::Change TrackList::setTrackInputPort(int id, const std::string& port)
{
    const auto* track = m_project.findTrack(id);
    if (!track || trackLocked(id)) return {};
    if (track->inputPort == port) return {true, false};
    return result(m_project.setTrackInputPort(id, port));
}
TrackList::MonitorState TrackList::cleanMonitor() const
{
    const bool soloed = std::any_of(m_project.tracks().begin(), m_project.tracks().end(), [](const auto& t) { return t->solo; });
    for (const auto& track : m_project.tracks()) if (track->hasLiveInput() && track->monitorEnabled)
        return {true, track->inputPort, track->muted || (soloed && !track->solo) ? 0 : track->volume, track->eqGainDb};
    return {};
}
bool TrackList::isMonitored(int id) const
{
    const auto* track = m_project.findTrack(id);
    return track && track->hasLiveInput() && track->monitorEnabled;
}
int TrackList::recordingTrackId() const
{
    int selected = -1;
    for (const auto& track : m_project.tracks()) if (track->recordArmed) {
        if (!track->hasLiveInput() || selected != -1) return -1;
        selected = track->id;
    }
    return selected;
}
std::vector<TrackList::ClipInfo> TrackList::clips() const
{
    std::vector<ClipInfo> result;
    for (int row = 0; row < count(); ++row) {
        const auto& track = *m_project.tracks()[row];
        for (const auto& clip : track.audioClips) {
            const double start = m_project.tempo().beatAt(clip.start);
            result.push_back({clip.id, track.id, row, start,
                m_project.tempo().beatAt(clip.start + clip.length) - start,
                clip.poolId.empty() ? "Audio clip" : std::filesystem::path(clip.poolId).filename().string(),
                track.colour, clip.effectsPrinted, clip.gainDb});
        }
    }
    return result;
}
const AudioClip* TrackList::findClip(int trackId, int clipId) const
{
    const auto* track = m_project.findTrack(trackId);
    if (track) for (const auto& clip : track->audioClips) if (clip.id == clipId) return &clip;
    return nullptr;
}
int TrackList::addAudioClip(int id, const std::string& pool, std::int64_t start, std::int64_t length, std::int64_t offset, bool printed)
{
    AudioClip clip; clip.poolId = pool; clip.start = start; clip.length = length;
    clip.sourceOffset = offset; clip.effectsPrinted = printed;
    return m_project.addAudioClip(id, std::move(clip));
}


int TrackList::addClipAtBeat(int trackId, double startBeat, double lengthBeats)
{
    if (editsLocked()) return -1;
    if (!std::isfinite(startBeat) || !std::isfinite(lengthBeats) || startBeat < 0.0 || lengthBeats <= 0.0)
        return -1;

    const auto start = sampleAtBeat(m_project.tempo(), startBeat);
    const auto end = sampleAtBeat(m_project.tempo(), startBeat + lengthBeats);
    if (!start || !end) return -1;
    const auto startSample = *start, endSample = *end;
    if (endSample <= startSample)
        return -1;

    AudioClip clip;
    clip.poolId = "Clip " + std::to_string(m_nextClipNumber++);
    clip.start = startSample;
    clip.length = endSample - startSample;
    const int clipId = m_project.addAudioClip(trackId, std::move(clip));
    return clipId;
}

bool TrackList::trimClipStartAtBeat(int trackId, int clipId, double startBeat) {
    const double sample = startBeat * 60 * m_project.tempo().sampleRate() / m_project.tempo().bpm();
    if (!m_recordingTracks.empty() || !std::isfinite(sample) || sample < 0 || sample >= double(INT64_MAX/4)) return false;
    const auto* track = m_project.findTrack(trackId); if (!track) return false;
    for (const auto& c : track->audioClips) if (c.id==clipId) {
        const auto audio=audioFor(c.poolId);
        const double ratio=audio ? audio->sampleRate/m_project.tempo().sampleRate() : 1.0;
        const std::int64_t start=std::llround(sample);
        const double offset=c.sourceOffset+(start-c.start)*ratio;
        if (offset<0 || offset>=double(INT64_MAX/4) || start>=c.start+c.length) return false;
        if (!m_project.setClipBounds(trackId,clipId,start,c.start+c.length-start,std::llround(offset))) return false;
         return true;
    }
    return false;
}

int TrackList::splitClipAtBeat(int trackId, int clipId, double beat) {
    const double sample=beat*60*m_project.tempo().sampleRate()/m_project.tempo().bpm();
    if (!m_recordingTracks.empty() || !std::isfinite(sample) || sample<0 || sample>=double(INT64_MAX/4)) return -1;
    const auto* track=m_project.findTrack(trackId); if (!track) return -1;
    for (const auto& c : track->audioClips) if(c.id==clipId) {
        const auto audio=audioFor(c.poolId);
        const double ratio=audio ? audio->sampleRate/m_project.tempo().sampleRate() : 1.0;
        const std::int64_t position=std::llround(sample);
        const double offset=c.sourceOffset+(position-c.start)*ratio;
        if(offset<0 || offset>=double(INT64_MAX/4))return -1;
        const int id=m_project.splitAudioClip(trackId,clipId,position,std::llround(offset));
        return id;
    }
    return -1;
}

TrackList::Change TrackList::editClips(const std::vector<int>& ids, const std::string& operation, double deltaBeats, int laneDelta) {
    if (!m_recordingTracks.empty() || ids.empty() || !std::isfinite(deltaBeats)
        || (operation!="move" && operation!="delete" && operation!="trimStart" && operation!="resize")) return {};
    const double samples=deltaBeats*60*m_project.tempo().sampleRate()/m_project.tempo().bpm();
    if (!std::isfinite(samples) || std::abs(samples)>=double(INT64_MAX/4)) return {};
    const std::int64_t delta=std::llround(samples);
    if (laneDelta!=0 && operation!="move") return {};
    if (operation!="delete" && delta==0 && laneDelta==0) return {true, false};
    std::vector<Project::ClipChange> changes;
    for (int id:ids) {
        bool found=false;
        for (const auto& track:m_project.tracks()) for (const auto& original:track->audioClips) if (original.id==id) {
            found=true; auto clip=original;
            // Check the supported edit range before signed additions, not after overflow.
            if (clip.start >= INT64_MAX/2 || clip.length >= INT64_MAX/2) return {};
            const auto audio=audioFor(clip.poolId);
            const double ratio=audio ? audio->sampleRate/m_project.tempo().sampleRate() : 1.0;
            if (operation=="move" || operation=="trimStart") clip.start+=delta;
            if (operation=="resize") clip.length+=delta;
            if (operation=="trimStart") {
                clip.length-=delta;
                const double offset=clip.sourceOffset+delta*ratio;
                if (offset<0 || offset>=double(INT64_MAX/4)) return {};
                clip.sourceOffset=std::llround(offset);
            }
            if (clip.start<0 || clip.length<=0 || clip.start>=INT64_MAX/2 || clip.length>=INT64_MAX/2) return {};
            if (operation!="delete" && operation!="move" && audio
                && clip.length>std::llround((audio->frames-clip.sourceOffset)/ratio)) return {};
            const std::int64_t destination=std::int64_t(indexOfTrack(track->id))+laneDelta;
            if (destination<0 || destination>=count()) return {};
            changes.push_back({track->id,clip,operation=="delete",trackIdAt(int(destination))});
        }
        if (!found) return {};
    }
    if (!m_project.editClipGroup(changes)) return {};
     return {true, true};
}

bool TrackList::canMergeClips(int trackId, int first, int second) const {
    if (!m_recordingTracks.empty() || first==second) return false;
    const auto* track=m_project.findTrack(trackId); if (!track) return false;
    const AudioClip *a=nullptr,*b=nullptr;
    for (const auto& clip:track->audioClips) { if (clip.id==first)a=&clip; if (clip.id==second)b=&clip; }
    if (!a || !b) return false;
    const auto blank=[&](const AudioClip& clip) {
        const auto& name=clip.poolId;
        return name.starts_with("Clip ") && !m_mediaPaths.contains(name) && !m_audio.contains(name);
    };
    if (blank(*a) && blank(*b)) {
        if (a->start>b->start)std::swap(a,b);
        return a->start+a->length==b->start && a->effectsPrinted==b->effectsPrinted && a->gainDb==b->gainDb
            && a->fadeOut==0 && b->fadeIn==0;
    }
    const auto audio=audioFor(a->poolId);
    return m_project.canMergeAudioClips(trackId,first,second,audio ? audio->sampleRate : m_project.tempo().sampleRate());
}

bool TrackList::canMergeSelectedClips(const std::vector<int>& ids) const {
    if (!m_recordingTracks.empty() || ids.size()<2) return false;
    std::vector<const AudioClip*> clips; int trackId=-1; std::unordered_set<int> seen;
    for (int id:ids) {
        if (seen.contains(id)) return false;
        seen.insert(id); bool found=false;
        for (const auto& track:m_project.tracks()) for (const auto& clip:track->audioClips) if (clip.id==id) {
            if (trackId>=0 && trackId!=track->id) return false;
            trackId=track->id; clips.push_back(&clip); found=true;
        }
        if (!found) return false;
    }
    std::sort(clips.begin(),clips.end(),[](const auto* a,const auto* b){return a->start<b->start;});
    for (std::size_t i=1;i<clips.size();++i) if (!canMergeClips(trackId,clips[i-1]->id,clips[i]->id)) return false;
    return true;
}

bool TrackList::mergeSelectedClips(const std::vector<int>& ids) {
    if (!canMergeSelectedClips(ids)) return false;
    std::vector<AudioClip> clips; int trackId=-1;
    for (const auto& track:m_project.tracks()) for (const auto& clip:track->audioClips)
        if (std::find(ids.begin(),ids.end(),clip.id)!=ids.end()) { clips.push_back(clip); trackId=track->id; }
    std::sort(clips.begin(),clips.end(),[](const auto& a,const auto& b){return a.start<b.start;});
    auto merged=clips.front(); merged.length=clips.back().start+clips.back().length-merged.start; merged.fadeOut=clips.back().fadeOut;
    std::vector<Project::ClipChange> changes{{trackId,merged}};
    for (std::size_t i=1;i<clips.size();++i)changes.push_back({trackId,clips[i],true});
    if (!m_project.editClipGroup(changes)) return false;
     return true;
}

bool TrackList::mergeClips(int trackId, int first, int second) {
    return canMergeClips(trackId,first,second) && mergeSelectedClips({first,second});
}

bool TrackList::moveClipAtBeat(int trackId, int clipId, double startBeat) {
    const double samples = startBeat * 60.0 * m_project.tempo().sampleRate() / m_project.tempo().bpm();
    if (!m_recordingTracks.empty() || !std::isfinite(samples) || samples < 0 || samples >= double(INT64_MAX / 4)) return false;
    if (!m_project.moveAudioClip(trackId, clipId, std::llround(samples))) return false;
     return true;
}

bool TrackList::resizeClipAtBeat(int trackId, int clipId, double lengthBeats)
{
    if (!m_recordingTracks.empty() || !std::isfinite(lengthBeats) || lengthBeats <= 0.0)
        return false;
    const Track* track = m_project.findTrack(trackId);
    if (!track)
        return false;
    const auto clip = std::find_if(track->audioClips.begin(), track->audioClips.end(), [clipId](const AudioClip& item) {
        return item.id == clipId;
    });
    if (clip == track->audioClips.end())
        return false;

    const double startBeat = m_project.tempo().beatAt(clip->start);
    const auto end = sampleAtBeat(m_project.tempo(), startBeat + lengthBeats);
    if (!end) return false;
    const auto endSample = *end;
    const auto audio=audioFor(clip->poolId);
    if (audio && endSample-clip->start > std::llround((audio->frames-clip->sourceOffset)*m_project.tempo().sampleRate()/audio->sampleRate)) return false;
    if (endSample <= clip->start || !m_project.resizeAudioClip(trackId, clipId, endSample - clip->start))
        return false;

    return true;
}

TrackList::Change TrackList::setClipGain(int trackId, int clipId, double gainDb)
{
    if (editsLocked()) return {};
    const auto* clip = findClip(trackId, clipId);
    if (!clip || !std::isfinite(gainDb)) return {};
    const float clamped = std::clamp(static_cast<float>(gainDb), -120.0f, 24.0f);
    if (clip->gainDb == clamped) return {true, false};
    const bool ok = m_project.setClipGain(trackId, clipId, clamped);
    return {ok, ok};
}

bool TrackList::removeAudioClip(int trackId, int clipId)
{
    if (editsLocked()) return false;
    if (m_selectedClipId == clipId) m_selectedClipId = -1;
    if (!m_project.removeAudioClip(trackId, clipId))
        return false;
    return true;
}

bool TrackList::copyClips(const std::vector<int>& ids)
{
    if (ids.empty()) return false;
    struct Candidate {
        int row;
        AudioClip clip;
        double startBeat;
        double lengthBeats;
    };
    std::vector<Candidate> candidates;
    double minStart = std::numeric_limits<double>::infinity();
    int minRow = std::numeric_limits<int>::max();
    for (int id : ids) {
        for (int row = 0; row < count(); ++row) {
            const auto& track = *m_project.tracks()[row];
            for (const auto& clip : track.audioClips) {
                if (clip.id == id) {
                    const double start = m_project.tempo().beatAt(clip.start);
                    const double length = m_project.tempo().beatAt(clip.start + clip.length) - start;
                    minStart = std::min(minStart, start);
                    minRow = std::min(minRow, row);
                    candidates.push_back({row, clip, start, length});
                    break;
                }
            }
        }
    }
    if (candidates.empty()) return false;
    m_clipboard.clear();
    m_clipboard.reserve(candidates.size());
    for (const auto& c : candidates) {
        ClipboardClip item;
        item.poolId = c.clip.poolId;
        item.beatOffset = c.startBeat - minStart;
        item.lengthBeats = c.lengthBeats;
        item.sourceOffset = c.clip.sourceOffset;
        item.fadeIn = c.clip.fadeIn;
        item.fadeOut = c.clip.fadeOut;
        item.gainDb = c.clip.gainDb;
        item.effectsPrinted = c.clip.effectsPrinted;
        item.trackOffset = c.row - minRow;
        m_clipboard.push_back(std::move(item));
    }
    return !m_clipboard.empty();
}

std::vector<int> TrackList::cutClips(const std::vector<int>& ids)
{
    if (editsLocked() || ids.empty()) return {};
    if (!copyClips(ids)) return {};
    if (!editClips(ids, "delete", 0)) {
        m_clipboard.clear();
        return {};
    }
    return ids;
}

std::vector<int> TrackList::pasteClips(int targetTrackId, double targetBeat)
{
    if (m_clipboard.empty() || editsLocked() || count() == 0) return {};
    if (!std::isfinite(targetBeat) || targetBeat < 0.0) targetBeat = 0.0;
    int targetRow = indexOfTrack(targetTrackId);
    if (targetRow < 0) targetRow = 0;

    std::vector<std::pair<int, AudioClip>> toAdd;
    toAdd.reserve(m_clipboard.size());
    for (const auto& item : m_clipboard) {
        int destRow = targetRow + item.trackOffset;
        if (destRow < 0) destRow = 0;
        if (destRow >= count()) destRow = count() - 1;
        const int destTrackId = trackIdAt(destRow);
        const double clipStartBeat = targetBeat + item.beatOffset;
        const auto startSample = sampleAtBeat(m_project.tempo(), clipStartBeat);
        const auto endSample = sampleAtBeat(m_project.tempo(), clipStartBeat + item.lengthBeats);
        if (!startSample || !endSample || *endSample <= *startSample) continue;

        AudioClip clip;
        clip.poolId = item.poolId;
        clip.start = *startSample;
        clip.length = *endSample - *startSample;
        clip.sourceOffset = item.sourceOffset;
        clip.fadeIn = item.fadeIn;
        clip.fadeOut = item.fadeOut;
        clip.gainDb = item.gainDb;
        clip.effectsPrinted = item.effectsPrinted;
        toAdd.emplace_back(destTrackId, std::move(clip));
    }
    if (toAdd.empty()) return {};
    return m_project.addAudioClips(toAdd);
}

std::vector<int> TrackList::duplicateClips(const std::vector<int>& ids, double beatOffset)
{
    if (editsLocked() || ids.empty() || count() == 0) return {};
    struct FoundClip {
        int trackId;
        AudioClip clip;
        double startBeat;
        double lengthBeats;
    };
    std::vector<FoundClip> found;
    double minStart = std::numeric_limits<double>::infinity();
    double maxEnd = -std::numeric_limits<double>::infinity();
    for (int id : ids) {
        for (const auto& track : m_project.tracks()) {
            for (const auto& clip : track->audioClips) {
                if (clip.id == id) {
                    const double start = m_project.tempo().beatAt(clip.start);
                    const double length = m_project.tempo().beatAt(clip.start + clip.length) - start;
                    minStart = std::min(minStart, start);
                    maxEnd = std::max(maxEnd, start + length);
                    found.push_back({track->id, clip, start, length});
                    break;
                }
            }
        }
    }
    if (found.empty()) return {};
    double offset = beatOffset;
    if (offset <= 0.0) {
        offset = maxEnd - minStart;
        if (offset <= 0.0) offset = 1.0;
    }
    std::vector<std::pair<int, AudioClip>> toAdd;
    toAdd.reserve(found.size());
    for (const auto& f : found) {
        const double newStartBeat = f.startBeat + offset;
        const auto startSample = sampleAtBeat(m_project.tempo(), newStartBeat);
        const auto endSample = sampleAtBeat(m_project.tempo(), newStartBeat + f.lengthBeats);
        if (!startSample || !endSample || *endSample <= *startSample) continue;

        AudioClip clip = f.clip;
        clip.start = *startSample;
        clip.length = *endSample - *startSample;
        toAdd.emplace_back(f.trackId, std::move(clip));
    }
    if (toAdd.empty()) return {};
    return m_project.addAudioClips(toAdd);
}

bool TrackList::undo() {
    if (!canUndo()) return false;
    m_project.undo();  return true;
}

bool TrackList::redo() {
    if (!canRedo()) return false;
    m_project.redo();  return true;
}

bool TrackList::setTempo(double bpm, int numerator, int denominator)
{
    if (!std::isfinite(bpm) || bpm <= 0.0 || numerator <= 0 || denominator <= 0)
        return false;
    m_project.tempo().set(bpm, numerator, denominator, m_project.tempo().sampleRate());
    return true;
}

void TrackList::setSampleRate(double rate)
{
    if (std::isfinite(rate) && rate >= 1000 && rate <= 768000) {
        const auto& t = m_project.tempo();
        m_project.tempo().set(t.bpm(), t.numerator(), t.denominator(), rate);
    }
}

bool TrackList::attachTake(int trackId, std::int64_t start, std::shared_ptr<const AudioData> audio, bool effectsPrinted)
{
    if (!audio || audio->frames <= 0 || !std::isfinite(audio->sampleRate) || audio->sampleRate <= 0
        || !m_project.findTrack(trackId)) return false;
    const double length = audio->frames * m_project.tempo().sampleRate() / audio->sampleRate;
    if (!std::isfinite(length) || length < 1 || length >= double(INT64_MAX/4)) return false;
    const auto& path = audio->path;
    m_audio[path] = audio; m_mediaPaths.insert(path);
    return addAudioClip(trackId, path, start, std::llround(length), 0, effectsPrinted) >= 0;
}
