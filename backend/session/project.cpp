#include "project.h"

#include <algorithm>
#include <cmath>
#include <limits>

Track& Project::addTrack(TrackKind kind, const std::string& name, const std::string& type)
{
    auto track = std::make_unique<Track>();
    track->id = m_nextId++;
    track->kind = kind;
    track->name = name;
    track->type = type.empty() ? (kind == TrackKind::Instrument ? "Instrument" : "Audio") : type;
    m_tracks.push_back(std::move(track));
    return *m_tracks.back();
}

void Project::removeTrack(int id)
{
    const auto it = std::find_if(m_tracks.begin(), m_tracks.end(), [id](const auto& track) {
        return track->id == id;
    });
    if (it != m_tracks.end())
        m_tracks.erase(it);
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
    track->audioClips.push_back(std::move(clip));
    return track->audioClips.back().id;
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
    track->audioClips.erase(it);
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
    it->length = lengthSamples;
    return true;
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
