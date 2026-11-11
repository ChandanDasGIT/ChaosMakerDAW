#include "tracklist.h"
#include "dsp/panlaw.h"
#include "mixer/mixerpanel.h"
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

namespace {
// Names the undo step an action adds (restMap G16): when the action ends, by whichever return,
// the step it added (if any) gets this name. Actions that call others (Cut calls Delete) name
// the step last, so the outer name wins. Labels are string literals.
class StepName {
public:
    StepName(Project& project, const char* label) : m_project(project), m_steps(project.stepCount()), m_label(label) {}
    ~StepName() { if (m_project.stepCount() != m_steps) m_project.setLastStepLabel(m_label); }
    StepName(const StepName&) = delete;
    StepName& operator=(const StepName&) = delete;
private:
    Project& m_project;
    std::uint64_t m_steps;
    const char* m_label;
};

const char* clipEditLabel(const std::string& operation, bool one)
{
    if (operation == "move") return one ? "Move clip" : "Move clips";
    if (operation == "delete") return one ? "Delete clip" : "Delete clips";
    if (operation == "trimStart") return one ? "Trim clip" : "Trim clips";
    return one ? "Resize clip" : "Resize clips";
}
} // namespace

namespace {
constexpr std::string_view reversedSuffix = "\x1freversed";
}

std::string TrackList::reversedPoolId(const std::string& poolId)
{
    return poolId + std::string(reversedSuffix);
}

std::shared_ptr<const AudioData> TrackList::audioFor(const std::string& path) const
{
    if (path.size() > reversedSuffix.size() && path.ends_with(reversedSuffix)) {
        const std::string base = path.substr(0, path.size() - reversedSuffix.size());
        const auto original = audioFor(base);
        if (!original) return nullptr;
        std::lock_guard lock(m_reversed->mutex);
        auto& copy = m_reversed->copies[base];
        if (!copy || copy->frames != original->frames) {
            auto reversed = std::make_shared<AudioData>(*original);
            for (auto& channel : reversed->channels) std::reverse(channel.begin(), channel.end());
            reversed->buildWaveform();
            copy = std::move(reversed);
        }
        return copy;
    }
    const auto found = m_audio.find(path);
    return found == m_audio.end() ? nullptr : found->second;
}

std::shared_ptr<const AudioData> TrackList::clipAudio(const AudioClip& clip) const
{
    return audioFor(clip.reversed ? reversedPoolId(clip.poolId) : clip.poolId);
}

int TrackList::splitForRangeEffects(int trackId, double startBeat, double endBeat)
{
    const auto* track = m_project.findTrack(trackId);
    const auto from = sampleAtBeat(m_project.tempo(), startBeat);
    const auto to = sampleAtBeat(m_project.tempo(), endBeat);
    if (!m_recordingTracks.empty() || !track || !from || !to || *to <= *from) return -1;
    const AudioClip* best = nullptr;
    std::int64_t bestOverlap = 0;
    for (const auto& clip : track->audioClips) {
        if (isPatternClip(clip)) continue;
        const std::int64_t overlap = std::min(*to, clip.start + clip.length) - std::max(*from, clip.start);
        if (overlap > bestOverlap) { best = &clip; bestOverlap = overlap; }
    }
    if (!best) return -1;
    const int original = best->id;
    const std::int64_t clipStart = best->start, clipEnd = best->start + best->length;
    const double beatsPerSample = m_project.tempo().bpm() / (60.0 * m_project.tempo().sampleRate());
    int middle = original, splits = 0;
    if (*from > clipStart) {
        middle = splitClipAtBeat(trackId, original, double(*from) * beatsPerSample);
        if (middle < 0) return -1;
        ++splits;
    }
    if (*to < clipEnd) {
        if (splitClipAtBeat(trackId, middle, double(*to) * beatsPerSample) < 0) {
            if (splits) undo();
            return -1;
        }
        ++splits;
    }
    if (splits == 2) m_project.joinLastClipEdits();   // one command, one undo step
    return middle;
}

bool TrackList::canHaveClipEffects(int clipId) const
{
    if (!m_recordingTracks.empty()) return false;
    const auto* clip = m_project.clipOfEffects(Project::clipEffectsId(clipId));
    return clip && !isPatternClip(*clip);
}

bool TrackList::canApplyClipTool(const std::vector<int>& ids) const
{
    if (!m_recordingTracks.empty()) return false;
    for (const auto& track : m_project.tracks())
        for (const auto& clip : track->audioClips)
            if (std::find(ids.begin(), ids.end(), clip.id) != ids.end() && !isPatternClip(clip) && audioFor(clip.poolId))
                return true;
    return false;
}

TrackList::Change TrackList::applyClipTool(const std::vector<int>& ids, ClipTool tool)
{
    const StepName step(m_project, tool == ClipTool::Reverse ? "Reverse" : tool == ClipTool::InvertPolarity ? "Invert polarity" : "Normalise");
    if (!canApplyClipTool(ids)) return {};
    std::vector<Project::ClipChange> changes;
    for (const auto& track : m_project.tracks()) {
        for (const auto& original : track->audioClips) {
            if (std::find(ids.begin(), ids.end(), original.id) == ids.end() || isPatternClip(original)) continue;
            const auto audio = clipAudio(original);
            if (!audio || audio->frames <= 0) continue;
            auto clip = original;
            const double ratio = audio->sampleRate / m_project.tempo().sampleRate();
            const std::int64_t used = std::min<std::int64_t>(audio->frames - clip.sourceOffset, std::llround(double(clip.length) * ratio));
            if (tool == ClipTool::Reverse) {
                // The same stretch of the file, counted from the other end of the reversed copy.
                clip.sourceOffset = std::max<std::int64_t>(0, audio->frames - (clip.sourceOffset + used));
                clip.reversed = !clip.reversed;
            } else if (tool == ClipTool::InvertPolarity) {
                clip.polarityInverted = !clip.polarityInverted;
            } else {
                float peak = 0.0f;
                for (const auto& channel : audio->channels)
                    for (std::int64_t i = clip.sourceOffset; i < clip.sourceOffset + used && i < std::int64_t(channel.size()); ++i)
                        peak = std::max(peak, std::abs(channel[std::size_t(i)]));
                if (peak <= 0.0f || !std::isfinite(peak)) continue;   // silence: nothing to normalise
                clip.gainDb = std::clamp(-20.0f * std::log10(peak), -120.0f, 24.0f);
                if (clip.gainDb == original.gainDb) continue;
            }
            changes.push_back({track->id, clip, false, track->id});
        }
    }
    if (changes.empty()) return {true, false};
    if (!m_project.editClipGroup(changes)) return {};
    return {true, true};
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
    const auto newTrackRecordDry = m_newTrackRecordDry; // an app preference, not part of the document
    *this = std::move(candidate);
    m_newTrackRecordDry = newTrackRecordDry;
    m_recordingTracks.clear();
    m_project.clearHistory();
}

std::vector<TrackList::TrackChoice> TrackList::trackChoices()
{
    std::vector<TrackChoice> choices{{"Vocal", "Vocal", false}, {"Instrument", "Instrument", true}};
    for (const auto& entry : instrumentcatalog::instruments())
        if (entry.ownKind) choices.push_back({std::string(entry.id), std::string(entry.name), false});
    return choices;
}

std::vector<TrackList::TrackChoice> TrackList::instrumentChoices()
{
    std::vector<TrackChoice> choices;
    for (const auto& entry : instrumentcatalog::instruments()) {
        if (entry.ownKind) continue;                       // listed beside Vocal and Instrument
        if (entry.group.empty()) {
            choices.push_back({std::string(entry.id), std::string(entry.name), false});
            continue;
        }
        const std::string id = std::string(GroupPrefix) + std::string(entry.group);
        const bool listed = std::any_of(choices.begin(), choices.end(), [&id](const auto& c) { return c.id == id; });
        if (!listed) choices.push_back({id, std::string(entry.group), true});
    }
    return choices;
}

std::vector<TrackList::TrackChoice> TrackList::variantChoices(const std::string& group)
{
    std::string_view name = group;
    if (name.starts_with(GroupPrefix)) name.remove_prefix(GroupPrefix.size());
    std::vector<TrackChoice> choices;
    if (name.empty()) return choices;
    for (const auto& entry : instrumentcatalog::instruments())
        if (entry.group == name) choices.push_back({std::string(entry.id), std::string(entry.variant), false});
    return choices;
}

bool TrackList::isInstrumentType(const std::string& type) { return instrumentcatalog::find(type); }
bool TrackList::canAddChoice(const std::string& id) const { return id == "Vocal" || isInstrumentType(id); }
int TrackList::addTrackChoice(const std::string& id) { return id == "Vocal" ? addVocalTrack() : addInstrumentTrackOfType(id); }
int TrackList::startTrack(Track& track)
{
    if (m_newTrackRecordDry) track.recordDry = *m_newTrackRecordDry;
    return track.id;
}
int TrackList::addAudioTrack()
{
    const int id = startTrack(m_project.addTrack(TrackKind::Audio, "Audio " + std::to_string(m_nextAudioNumber++), "Audio"));
    if (m_selectedTrackId < 0) m_selectedTrackId = id;
    return id;
}
int TrackList::addVocalTrack()
{
    const int id = startTrack(m_project.addTrack(TrackKind::Audio, "Vocal " + std::to_string(m_nextVocalNumber++), "Vocal"));
    if (m_selectedTrackId < 0) m_selectedTrackId = id;
    return id;
}
int TrackList::addInstrumentTrack()
{
    const int id = startTrack(m_project.addTrack(TrackKind::Instrument, "Instrument " + std::to_string(m_nextInstrumentNumber++), "Instrument"));
    if (m_selectedTrackId < 0) m_selectedTrackId = id;
    return id;
}
int TrackList::addInstrumentTrackOfType(const std::string& type)
{
    if (!isInstrumentType(type)) return -1;
    const int id = startTrack(m_project.addTrack(TrackKind::Instrument, type + ' ' + std::to_string(++m_instrumentNumbers[type]), type));
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
// Only a track in the list: the master track (G9) is found by id but cannot be removed.
bool TrackList::canRemoveTrack(int id) const { return !trackLocked(id) && indexOfTrack(id) >= 0; }
bool TrackList::removeTrack(int id)
{
    if (!canRemoveTrack(id)) return false;
    const int removedRow = indexOfTrack(id);
    m_project.removeTrack(id);
    selectNeighbourOfRemoved(id, removedRow);
    return true;
}

void TrackList::selectNeighbourOfRemoved(int id, int removedRow)
{
    if (m_selectedTrackId != id) return;
    m_selectedTrackId = count() > 0 ? trackIdAt(std::min(removedRow, count() - 1)) : -1;
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
TrackList::Change TrackList::setPan(int id, double value)
{
    const auto* track = m_project.findTrack(id);
    if (!track || !std::isfinite(value)) return {};
    if (track->pan == static_cast<float>(panlaw::clampPan(value))) return {true, false};
    return result(m_project.setTrackPan(id, static_cast<float>(value)));
}
TrackList::Change TrackList::setStripWidth(int id, double value)
{
    const auto* track = m_project.findTrack(id);
    if (!track || !std::isfinite(value)) return {};
    if (track->stripWidth == static_cast<float>(mixer::clampStripWidth(value))) return {true, false};
    return result(m_project.setTrackStripWidth(id, static_cast<float>(value)));
}
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
TrackList::Change TrackList::setChaosEq(int id, const chaoseq::Settings& settings)
{
    return result(m_project.setTrackChaosEq(id, settings));
}
std::optional<mixfx::Settings> TrackList::mixEffect(int id, std::string_view type) const
{
    const auto* track = m_project.effects(id);   // a track, the master or a clip
    if (!track || !mixfx::find(type)) return std::nullopt;
    return track->mixEffect(type);
}
TrackList::Change TrackList::setMixEffect(int id, std::string_view type, const mixfx::Settings& settings)
{
    const auto before = mixEffect(id, type);
    if (!before) return {};
    if (!m_project.setTrackMixEffect(id, type, settings)) return {};
    return {true, *before != *mixEffect(id, type)};
}
TrackList::Change TrackList::setHarmonium(int id, const harmonium::Settings& settings)
{
    return result(m_project.setTrackHarmonium(id, settings));
}
TrackList::Change TrackList::setDrumKit(int id, const drumkit::Settings& settings)
{
    return result(m_project.setTrackDrumKit(id, settings));
}

TrackList::Change TrackList::setElectroDrums(int id, const electrodrums::Settings& settings)
{
    return result(m_project.setTrackElectroDrums(id, settings));
}

TrackList::Change TrackList::setPiano(int id, const piano::Settings& settings)
{
    return result(m_project.setTrackPiano(id, settings));
}
TrackList::Change TrackList::setChords(int id, const chords::Settings& settings)
{
    return result(m_project.setTrackChords(id, settings));
}
int TrackList::addTrackEffect(int trackId, const std::string& type)
{
    if (Project::isClipEffectsId(trackId) && !canHaveClipEffects(trackId - Project::ClipEffectsBase)) return -1;   // audio clips only (R2)
    return trackLocked(trackId) ? -1 : m_project.addTrackEffect(trackId, type);
}
TrackList::Change TrackList::removeTrackEffect(int trackId, int effectId)
{
    return trackLocked(trackId) ? Change{} : result(m_project.removeTrackEffect(trackId, effectId));
}
TrackList::Change TrackList::moveTrackEffect(int trackId, int effectId, int toIndex)
{
    return trackLocked(trackId) ? Change{} : result(m_project.moveTrackEffect(trackId, effectId, toIndex));
}
TrackList::Change TrackList::setMonitorEnabled(int id, bool value)
{
    auto* track = m_project.findTrack(id);
    if (!track || !track->canMonitor()) return {};
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
            const bool blank = isPatternClip(clip);
            result.push_back({clip.id, track.id, row, start,
                m_project.tempo().beatAt(clip.start + clip.length) - start,
                clip.poolId.empty() ? "Audio clip" : std::filesystem::path(clip.poolId).filename().string(),
                track.colour, clip.effectsPrinted, clip.gainDb, !blank,
                blank ? clipNotes(track.id, clip.id) : std::vector<MidiNote>{}, clip.reversed, clip.polarityInverted,
                clip.fx});
        }
        // Fades as played (restMap G8), over the track's audio clips.
        std::vector<FadeSpan> spans;
        std::vector<std::size_t> rows;
        for (std::size_t i = 0; i < track.audioClips.size(); ++i) {
            const auto& clip = track.audioClips[i];
            if (isPatternClip(clip)) continue;
            spans.push_back({clip.start, clip.length, clip.fadeIn, clip.fadeOut});
            rows.push_back(result.size() - track.audioClips.size() + i);
        }
        playedFades(spans);
        const auto& tempo = m_project.tempo();
        for (std::size_t i = 0; i < spans.size(); ++i) {
            auto& info = result[rows[i]];
            const auto end = spans[i].start + spans[i].length;
            info.fadeInBeats = tempo.beatAt(spans[i].start + spans[i].fadeIn) - info.startBeat;
            info.fadeOutBeats = tempo.beatAt(end) - tempo.beatAt(end - spans[i].fadeOut);
        }
    }
    return result;
}
bool TrackList::isPatternClip(const AudioClip& clip) const
{
    const auto& name = clip.poolId;
    return name.starts_with("Clip ") && !m_mediaPaths.contains(name) && !m_audio.contains(name);
}

// Pattern clips have no media, so sourceOffset is in project samples (ratio 1).
double TrackList::offsetBeats(const AudioClip& clip) const
{
    const auto& tempo = m_project.tempo();
    return double(clip.sourceOffset) * tempo.bpm() / (60.0 * tempo.sampleRate());
}

std::vector<MidiNote> TrackList::clipNotes(int trackId, int clipId) const
{
    const auto* clip = findClip(trackId, clipId);
    if (!clip) return {};
    auto notes = clip->notes;
    const double offset = offsetBeats(*clip);
    for (auto& n : notes) n.startBeat -= offset;
    return notes;
}

double TrackList::clipStartBeat(int trackId, int clipId) const
{
    const auto* clip = findClip(trackId, clipId);
    return clip ? m_project.tempo().beatAt(clip->start) : 0.0;
}

double TrackList::clipLengthBeats(int trackId, int clipId) const
{
    const auto* clip = findClip(trackId, clipId);
    if (!clip) return 0.0;
    const auto& tempo = m_project.tempo();
    return tempo.beatAt(clip->start + clip->length) - tempo.beatAt(clip->start);
}

TrackList::Change TrackList::setClipNotes(int trackId, int clipId, std::vector<MidiNote> notes)
{
    if (editsLocked()) return {};
    const auto* clip = findClip(trackId, clipId);
    if (!clip || !isPatternClip(*clip)) return {};
    const double offset = offsetBeats(*clip);
    for (auto& n : notes) {
        if (!std::isfinite(n.startBeat) || !std::isfinite(n.lengthBeats) || n.lengthBeats <= 0
            || n.note < 0 || n.note > 127 || n.velocity < 0 || n.velocity > 127) return {};
        n.startBeat += offset;
        n.selected = false;
    }
    if (clip->notes == notes) return {true, false};
    const bool ok = m_project.setClipNotes(trackId, clipId, std::move(notes));
    return {ok, ok};
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

int TrackList::addPatternClipAtBeat(int trackId, double startBeat, double lengthBeats, std::vector<MidiNote> notes)
{
    const StepName step(m_project, "Add pattern clip");
    if (editsLocked() || !m_project.findTrack(trackId)) return -1;
    if (!std::isfinite(startBeat) || !std::isfinite(lengthBeats) || startBeat < 0.0 || lengthBeats <= 0.0)
        return -1;
    for (auto& n : notes) {
        if (!std::isfinite(n.startBeat) || !std::isfinite(n.lengthBeats) || n.lengthBeats <= 0
            || n.note < 0 || n.note > 127 || n.velocity < 0 || n.velocity > 127) return -1;
        n.selected = false;                       // a new clip has no source offset: notes as given
    }
    const auto start = sampleAtBeat(m_project.tempo(), startBeat);
    const auto end = sampleAtBeat(m_project.tempo(), startBeat + lengthBeats);
    if (!start || !end || *end <= *start) return -1;

    AudioClip clip;
    clip.poolId = "Clip " + std::to_string(m_nextClipNumber++);
    clip.start = *start;
    clip.length = *end - *start;
    clip.notes = std::move(notes);
    return m_project.addAudioClip(trackId, std::move(clip));
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
    const StepName step(m_project, clipEditLabel(operation, ids.size() == 1));
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
    if (isPatternClip(*a) && isPatternClip(*b)) {
        if (a->start>b->start)std::swap(a,b);
        return a->start+a->length==b->start && a->effectsPrinted==b->effectsPrinted && a->gainDb==b->gainDb
            && a->reversed==b->reversed && a->polarityInverted==b->polarityInverted
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
    const StepName step(m_project, "Merge clips");
    if (!canMergeSelectedClips(ids)) return false;
    std::vector<AudioClip> clips; int trackId=-1;
    for (const auto& track:m_project.tracks()) for (const auto& clip:track->audioClips)
        if (std::find(ids.begin(),ids.end(),clip.id)!=ids.end()) { clips.push_back(clip); trackId=track->id; }
    std::sort(clips.begin(),clips.end(),[](const auto& a,const auto& b){return a.start<b.start;});
    auto merged=clips.front(); merged.length=clips.back().start+clips.back().length-merged.start; merged.fadeOut=clips.back().fadeOut;
    // Re-base the other clips' origin-relative notes onto the first clip's origin.
    const auto originBeats=[&](const AudioClip& c){ return m_project.tempo().beatAt(c.start)-offsetBeats(c); };
    for (std::size_t i=1;i<clips.size();++i) {
        const double shift=originBeats(clips[i])-originBeats(merged);
        for (auto n:clips[i].notes) { n.startBeat+=shift; merged.notes.push_back(n); }
    }
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

TrackList::Change TrackList::setClipGain(int trackId, int clipId, double gainDb, bool continueEdit)
{
    if (editsLocked()) return {};
    const auto* clip = findClip(trackId, clipId);
    if (!clip || !std::isfinite(gainDb)) return {};
    const float clamped = std::clamp(static_cast<float>(gainDb), -120.0f, 24.0f);
    if (clip->gainDb == clamped) return {true, false};
    const bool ok = m_project.setClipGain(trackId, clipId, clamped, continueEdit);
    return {ok, ok};
}

TrackList::Change TrackList::setClipFade(int trackId, int clipId, bool fadeOut, double beats, bool continueEdit)
{
    const StepName step(m_project, fadeOut ? "Fade out" : "Fade in");
    if (editsLocked() || !std::isfinite(beats)) return {};
    const auto* clip = findClip(trackId, clipId);
    if (!clip || isPatternClip(*clip)) return {};
    const auto& tempo = m_project.tempo();
    const double length = std::max(0.0, beats);
    std::int64_t samples = 0;
    if (fadeOut) {
        const auto end = clip->start + clip->length;
        const auto from = sampleAtBeat(tempo, std::max(0.0, tempo.beatAt(end) - length));
        if (!from) return {};
        samples = end - *from;
    } else {
        const auto to = sampleAtBeat(tempo, tempo.beatAt(clip->start) + length);
        if (!to) return {};
        samples = *to - clip->start;
    }
    // A fade reaches at most the other one, which stays (as the timeline's handle stops there).
    samples = std::clamp<std::int64_t>(samples, 0, clip->length - (fadeOut ? clip->fadeIn : clip->fadeOut));
    const auto in = fadeOut ? clip->fadeIn : samples;
    const auto out = fadeOut ? samples : clip->fadeOut;
    if (in == clip->fadeIn && out == clip->fadeOut) return {true, false};
    const bool ok = m_project.setClipFades(trackId, clipId, in, out, continueEdit);
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
    clearClipboard();
    m_clipboard.reserve(candidates.size());
    for (const auto& c : candidates) m_clipboardLengthBeats = std::max(m_clipboardLengthBeats, c.startBeat + c.lengthBeats - minStart);
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
        item.reversed = c.clip.reversed;
        item.polarityInverted = c.clip.polarityInverted;
        item.fx = c.clip.fx;
        item.trackOffset = c.row - minRow;
        item.notes = c.clip.notes;
        m_clipboard.push_back(std::move(item));
    }
    return !m_clipboard.empty();
}

std::vector<int> TrackList::cutClips(const std::vector<int>& ids, bool ripple)
{
    const StepName step(m_project, "Cut");
    if (editsLocked() || ids.empty()) return {};
    if (!copyClips(ids)) return {};
    if (!deleteClips(ids, ripple)) {
        clearClipboard();
        return {};
    }
    return ids;
}

std::vector<int> TrackList::pasteClips(int targetTrackId, double targetBeat, bool ripple)
{
    const StepName step(m_project, "Paste");
    if (!hasClipboard() || editsLocked() || count() == 0) return {};
    if (!std::isfinite(targetBeat) || targetBeat < 0.0) targetBeat = 0.0;
    int targetRow = indexOfTrack(targetTrackId);
    if (targetRow < 0) targetRow = 0;
    if (ripple || !m_clipboardEnvelopes.empty()) return pasteWithContent(targetRow, targetBeat, ripple);

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
        clip.reversed = item.reversed;
        clip.polarityInverted = item.polarityInverted;
        clip.fx = item.fx;
        clip.notes = item.notes;
        toAdd.emplace_back(destTrackId, std::move(clip));
    }
    if (toAdd.empty()) return {};
    return m_project.addAudioClips(toAdd);
}

std::vector<int> TrackList::duplicateClips(const std::vector<int>& ids, double beatOffset, bool ripple)
{
    const StepName step(m_project, "Duplicate");
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
    if (!ripple) return m_project.addAudioClips(toAdd);
    // Ripple (G15): everything from the end of the selection on, on every track, moves right
    // to make room for the copies, in the same undo step.
    const auto at = sampleAtBeat(m_project.tempo(), maxEnd);
    const auto until = sampleAtBeat(m_project.tempo(), minStart + offset + (maxEnd - minStart));
    if (!at || !until || *until <= *at) return m_project.addAudioClips(toAdd);
    std::vector<Project::TrackContent> contents;
    std::vector<int> added;
    for (const auto& track : m_project.tracks()) {
        Project::TrackContent content{track->id, clipsWithSpace(track->audioClips, *at, *until - *at),
                                      envelopeWithSpace(track->volumeEnvelope, *at, *until - *at)};
        for (auto& [trackId, clip] : toAdd) if (trackId == track->id) {
            clip.id = m_project.takeClipId();
            added.push_back(clip.id);
            content.clips.push_back(clip);
        }
        contents.push_back(std::move(content));
    }
    return applyContent(std::move(contents)).changed ? added : std::vector<int>{};
}

int TrackList::addEnvelopePoint(int trackId, std::int64_t position, double level)
{
    if (editsLocked()) return -1;
    return m_project.addEnvelopePoint(trackId, position, level);
}

TrackList::Change TrackList::moveEnvelopePoint(int trackId, int index, std::int64_t position, double level, bool continueEdit)
{
    const auto* track = m_project.findTrack(trackId);
    if (editsLocked() || !track) return {};
    const auto before = track->volumeEnvelope;
    if (!m_project.moveEnvelopePoint(trackId, index, position, level, continueEdit)) return {};
    return {true, !track->volumeEnvelope.sameContent(before)};
}

TrackList::Change TrackList::removeEnvelopePoint(int trackId, int index)
{
    if (editsLocked()) return {};
    const bool ok = m_project.removeEnvelopePoint(trackId, index);
    return {ok, ok};
}

TrackList::Change TrackList::setEnvelopePointShape(int trackId, int index, automation::CurveShape shape)
{
    const auto* track = m_project.findTrack(trackId);
    if (editsLocked() || !track) return {};
    const auto before = track->volumeEnvelope;
    if (!m_project.setEnvelopePointShape(trackId, index, shape)) return {};
    return {true, !track->volumeEnvelope.sameContent(before)};
}

TrackList::Change TrackList::setEnvelopeBypassed(int trackId, bool bypassed)
{
    const auto* track = m_project.findTrack(trackId);
    if (editsLocked() || !track) return {};
    if (track->volumeEnvelope.bypassed() == bypassed) return {true, false};
    const bool ok = m_project.setEnvelopeBypassed(trackId, bypassed);
    return {ok, ok};
}

TrackList::Change TrackList::setEnvelopeShown(int trackId, bool shown)
{
    const auto* track = m_project.findTrack(trackId);
    if (!track) return {};
    if (track->volumeEnvelope.shown() == shown) return {true, false};
    return {m_project.setEnvelopeShown(trackId, shown), true};
}

TrackList::Change TrackList::setEnvelopeOverClips(int trackId, bool overClips)
{
    const auto* track = m_project.findTrack(trackId);
    if (!track) return {};
    if (track->volumeEnvelope.overClips() == overClips) return {true, false};
    return {m_project.setEnvelopeOverClips(trackId, overClips), true};
}

int TrackList::addEnvelopePointAtBeat(int trackId, double beat, double level)
{
    const auto position = sampleAtBeat(m_project.tempo(), beat);
    return position ? addEnvelopePoint(trackId, *position, level) : -1;
}

TrackList::Change TrackList::moveEnvelopePointAtBeat(int trackId, int index, double beat, double level, bool continueEdit)
{
    // A drag past the start stops at 0 instead of being refused.
    const auto position = sampleAtBeat(m_project.tempo(), std::isfinite(beat) ? std::max(0.0, beat) : beat);
    if (!position) return {};
    return moveEnvelopePoint(trackId, index, *position, level, continueEdit);
}

std::vector<TrackList::EnvelopePointInfo> TrackList::envelopePointBeats(int trackId) const
{
    std::vector<EnvelopePointInfo> result;
    const auto* track = m_project.findTrack(trackId);
    if (!track) return result;
    for (const auto& point : track->volumeEnvelope.points())
        result.push_back({m_project.tempo().beatAt(point.position), point.level, point.shape});
    return result;
}

double TrackList::envelopeLevelAtBeat(int trackId, double beat) const
{
    const auto* track = m_project.findTrack(trackId);
    if (!track) return automation::envelopescale::unityLevel();
    const auto position = sampleAtBeat(m_project.tempo(), std::isfinite(beat) ? std::max(0.0, beat) : 0.0);
    return track->volumeEnvelope.levelAt(position.value_or(0));
}

std::vector<TrackList::EnvelopeCurveSample> TrackList::envelopeCurve(int trackId, double fromBeat, double toBeat, int steps) const
{
    std::vector<EnvelopeCurveSample> result;
    const auto* track = m_project.findTrack(trackId);
    if (!track || !std::isfinite(fromBeat) || !std::isfinite(toBeat) || toBeat < fromBeat || steps < 1 || steps > 100000)
        return result;
    const auto& tempo = m_project.tempo();
    const auto& envelope = track->volumeEnvelope;
    const auto levelAt = [&](double beat) {
        return envelope.levelAt(sampleAtBeat(tempo, std::max(0.0, beat)).value_or(0));
    };
    result.reserve(std::size_t(steps) + 1 + 2 * envelope.points().size());
    for (int i = 0; i <= steps; ++i) {
        const double beat = fromBeat + (toBeat - fromBeat) * i / steps;
        result.push_back({beat, levelAt(beat)});
    }
    for (const auto& point : envelope.points()) {
        const double beat = tempo.beatAt(point.position);
        if (beat < fromBeat || beat > toBeat) continue;
        if (point.position > 0) {
            const double before = tempo.beatAt(point.position - 1);
            if (before >= fromBeat) result.push_back({before, envelope.levelAt(point.position - 1)});
        }
        result.push_back({beat, point.level});
    }
    std::stable_sort(result.begin(), result.end(),
        [](const EnvelopeCurveSample& a, const EnvelopeCurveSample& b) { return a.beat < b.beat; });
    return result;
}

bool TrackList::undo() {
    if (!canUndo()) return false;
    m_project.undo();  return true;
}

bool TrackList::redo() {
    if (!canRedo()) return false;
    const auto removal = m_project.redoRemovesTrack();
    m_project.redo();
    if (removal) selectNeighbourOfRemoved(removal->trackId, static_cast<int>(removal->index));
    return true;
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

std::vector<int> TrackList::importAudio(const std::vector<std::shared_ptr<const AudioData>>& files, double startBeat,
                                        int targetTrackId, std::vector<int>* newTrackIds)
{
    const StepName step(m_project, "Import audio");
    std::vector<int> clips;
    const auto start = sampleAtBeat(m_project.tempo(), startBeat);
    if (editsLocked() || files.empty() || !start) return clips;
    if (targetTrackId >= 0 && indexOfTrack(targetTrackId) < 0) return clips;
    const auto mark = m_project.historyMark();
    std::vector<int> tracks;
    for (const auto& audio : files) {
        if (!audio || audio->frames <= 0 || !std::isfinite(audio->sampleRate) || audio->sampleRate <= 0) continue;
        const double length = double(audio->frames) * m_project.tempo().sampleRate() / audio->sampleRate;
        if (!std::isfinite(length) || length < 1 || length >= double(INT64_MAX / 4)) continue;
        int track = targetTrackId;
        if (track < 0) {
            const std::string name = std::filesystem::path(audio->path).stem().string();
            track = startTrack(m_project.addTrack(TrackKind::Audio, name.rfind("import-", 0) == 0 ? name.substr(7) : name, "Audio"));
            tracks.push_back(track);
        }
        m_audio[audio->path] = audio;
        m_mediaPaths.insert(audio->path);
        const int clip = addAudioClip(track, audio->path, *start, std::llround(length), 0, false);
        if (clip >= 0) clips.push_back(clip);
    }
    // One undo step: the new tracks go with their clips, or the clips on the one track go.
    if (!tracks.empty()) m_project.foldIntoAddedTracks(mark, tracks);
    else while (m_project.historyMark() > mark + 1 && m_project.joinLastClipEdits()) {}
    if (newTrackIds) *newTrackIds = tracks;
    if (m_selectedTrackId < 0 && !tracks.empty()) m_selectedTrackId = tracks.front();
    return clips;
}

bool TrackList::attachTake(int trackId, std::int64_t start, std::shared_ptr<const AudioData> audio, bool effectsPrinted)
{
    const StepName step(m_project, "Record");
    if (!audio || audio->frames <= 0 || !std::isfinite(audio->sampleRate) || audio->sampleRate <= 0
        || !m_project.findTrack(trackId)) return false;
    const double length = audio->frames * m_project.tempo().sampleRate() / audio->sampleRate;
    if (!std::isfinite(length) || length < 1 || length >= double(INT64_MAX/4)) return false;
    std::int64_t frames = std::llround(length), offset = 0;
    if (start < 0) {   // compensated to before the song's start: trim the front (G10)
        const std::int64_t trim = start <= -frames ? frames : -start;
        if (trim < frames) {
            offset = std::llround(double(trim) * audio->sampleRate / m_project.tempo().sampleRate());
            frames -= trim;
        }   // a take shorter than the shift stays whole, at 0
        start = 0;
    }
    const auto& path = audio->path;
    m_audio[path] = audio; m_mediaPaths.insert(path);
    return addAudioClip(trackId, path, start, frames, offset, effectsPrinted) >= 0;
}

bool TrackList::attachMidiTake(int trackId, std::int64_t start, std::int64_t length, std::vector<MidiNote> notes)
{
    const StepName step(m_project, "Record MIDI");
    if (length <= 0 || !m_project.findTrack(trackId)) return false;
    AudioClip clip;
    clip.poolId = "Clip " + std::to_string(m_nextClipNumber++);
    clip.start = start;
    clip.length = length;
    clip.notes = std::move(notes);
    return m_project.addAudioClip(trackId, std::move(clip)) >= 0;
}

// ---- Time selection edits and ripple (restMap G15) ----------------------------------------

namespace {
using automation::EnvelopePoint;
using automation::VolumeEnvelope;
// The level at `at` lies between two points (so removing what is around it would move the line).
bool pointsAround(const VolumeEnvelope& envelope, std::int64_t at)
{
    const auto& points = envelope.points();
    return !points.empty() && points.front().position < at && points.back().position > at;
}
bool pointAt(const std::vector<EnvelopePoint>& points, std::int64_t at)
{
    return std::any_of(points.begin(), points.end(), [at](const auto& p) { return p.position == at; });
}
// The shape of the segment that runs through `at` (the last point before it).
automation::CurveShape shapeThrough(const VolumeEnvelope& envelope, std::int64_t at)
{
    auto shape = automation::CurveShape::Linear;
    for (const auto& p : envelope.points()) if (p.position < at) shape = p.shape;
    return shape;
}
VolumeEnvelope withPoints(const VolumeEnvelope& envelope, std::vector<EnvelopePoint> points)
{
    std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
    points.erase(std::unique(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.position == b.position; }), points.end());
    VolumeEnvelope result = envelope;
    if (!result.setPoints(std::move(points))) return envelope;
    return result;
}
}

double TrackList::sourceRatio(const AudioClip& clip) const
{
    const auto audio = isPatternClip(clip) ? nullptr : audioFor(clip.poolId);
    return audio ? audio->sampleRate / m_project.tempo().sampleRate() : 1.0;
}

AudioClip TrackList::clipPiece(const AudioClip& clip, std::int64_t from, std::int64_t to) const
{
    AudioClip piece = clip;
    const std::int64_t end = clip.start + clip.length;
    from = std::max(from, clip.start);
    to = std::min(to, end);
    piece.start = from;
    piece.length = to - from;
    piece.sourceOffset = clip.sourceOffset + std::llround(double(from - clip.start) * sourceRatio(clip));
    piece.fadeIn = from == clip.start ? std::min(clip.fadeIn, piece.length) : 0;
    piece.fadeOut = to == end ? std::min(clip.fadeOut, piece.length - piece.fadeIn) : 0;
    if (!clip.notes.empty()) {
        // Notes are origin-relative (like a split): keep those that start inside the piece;
        // ones before the clip's own start stay with the piece at its start.
        const double first = offsetBeats(piece);
        const double last = first + double(piece.length) * m_project.tempo().bpm() / (60.0 * m_project.tempo().sampleRate());
        const bool keepsStart = from == clip.start, keepsEnd = to == end;
        piece.notes.clear();
        for (const auto& n : clip.notes)
            if ((n.startBeat >= first || keepsStart) && (n.startBeat < last || keepsEnd)) piece.notes.push_back(n);
    }
    return piece;
}

std::vector<AudioClip> TrackList::clipsWithoutSpan(const Track& track, std::int64_t from, std::int64_t to, bool ripple)
{
    const std::int64_t gap = ripple ? to - from : 0;
    std::vector<AudioClip> result;
    for (const auto& clip : track.audioClips) {
        const std::int64_t end = clip.start + clip.length;
        if (end <= from) { result.push_back(clip); continue; }
        if (clip.start >= to) { auto moved = clip; moved.start -= gap; result.push_back(moved); continue; }
        if (clip.start < from) result.push_back(clipPiece(clip, clip.start, from));
        if (end > to) {
            auto right = clipPiece(clip, to, end);
            if (clip.start < from) right.id = m_project.takeClipId();   // the clip went on both sides
            right.start -= gap;
            result.push_back(right);
        }
    }
    return result;
}

std::vector<AudioClip> TrackList::clipsWithSpace(const std::vector<AudioClip>& clips, std::int64_t at, std::int64_t length)
{
    std::vector<AudioClip> result;
    for (const auto& clip : clips) {
        const std::int64_t end = clip.start + clip.length;
        if (end <= at) { result.push_back(clip); continue; }
        if (clip.start >= at) { auto moved = clip; moved.start += length; result.push_back(moved); continue; }
        result.push_back(clipPiece(clip, clip.start, at));   // split where the space goes in
        auto right = clipPiece(clip, at, end);
        right.id = m_project.takeClipId();
        right.start += length;
        result.push_back(right);
    }
    return result;
}

VolumeEnvelope TrackList::envelopeWithoutSpan(const VolumeEnvelope& envelope, std::int64_t from, std::int64_t to, bool ripple)
{
    const auto& points = envelope.points();
    if (points.empty()) return envelope;
    std::vector<EnvelopePoint> kept;
    if (!ripple) {
        for (const auto& p : points) if (p.position <= from || p.position >= to) kept.push_back(p);
        if (pointsAround(envelope, from) && !pointAt(points, from))
            kept.push_back({from, envelope.levelAt(from), shapeThrough(envelope, from)});
        if (pointsAround(envelope, to) && !pointAt(points, to))
            kept.push_back({to, envelope.levelAt(to), shapeThrough(envelope, to)});
        return withPoints(envelope, std::move(kept));
    }
    // Ripple: what was at `to` lands on `from`, so the line before the range ends one sample
    // earlier.
    const std::int64_t gap = to - from;
    for (const auto& p : points) {
        if (p.position < from) kept.push_back(p);
        else if (p.position >= to) kept.push_back({p.position - gap, p.level, p.shape});
    }
    const bool before = points.front().position < from && points.back().position >= from;
    if (before && from > 0 && !pointAt(points, from - 1))
        kept.push_back({from - 1, envelope.levelAt(from - 1), shapeThrough(envelope, from - 1)});
    if (pointsAround(envelope, to) && !pointAt(points, to))
        kept.push_back({from, envelope.levelAt(to), shapeThrough(envelope, to)});
    return withPoints(envelope, std::move(kept));
}

VolumeEnvelope TrackList::envelopeWithSpace(const VolumeEnvelope& envelope, std::int64_t at, std::int64_t length)
{
    const auto& points = envelope.points();
    if (points.empty() || points.back().position < at) return envelope;
    std::vector<EnvelopePoint> moved;
    for (const auto& p : points) moved.push_back(p.position < at ? p : EnvelopePoint{p.position + length, p.level, p.shape});
    if (points.front().position < at) {
        // Flat across the new space: the level at `at` on both sides of it.
        const double level = envelope.levelAt(at);
        moved.push_back({at, level, automation::CurveShape::Linear});
        if (!pointAt(points, at)) moved.push_back({at + length, level, shapeThrough(envelope, at)});
    }
    return withPoints(envelope, std::move(moved));
}

VolumeEnvelope TrackList::envelopeWithPasted(const VolumeEnvelope& envelope, std::int64_t at, std::int64_t length,
                                             const std::vector<EnvelopePoint>& pasted)
{
    if (pasted.empty()) return envelope;
    const auto& points = envelope.points();
    const std::int64_t end = at + length;
    std::vector<EnvelopePoint> result;
    for (const auto& p : points) if (p.position < at || p.position > end) result.push_back(p);
    // The line just outside keeps its shape; the pasted line starts and ends on its own levels.
    if (!points.empty() && points.front().position < at - 1 && !pointAt(points, at - 1))
        result.push_back({at - 1, envelope.levelAt(at - 1), shapeThrough(envelope, at - 1)});
    if (!points.empty() && points.back().position > end + 1 && !pointAt(points, end + 1))
        result.push_back({end + 1, envelope.levelAt(end + 1), shapeThrough(envelope, end + 1)});
    for (const auto& p : pasted) {
        std::erase_if(result, [&](const auto& q) { return q.position == p.position; });
        result.push_back(p);
    }
    return withPoints(envelope, std::move(result));
}

bool TrackList::sameClips(const std::vector<AudioClip>& a, const std::vector<AudioClip>& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const AudioClip& x, const AudioClip& y) {
        return x.id == y.id && x.start == y.start && x.length == y.length && x.sourceOffset == y.sourceOffset
            && x.fadeIn == y.fadeIn && x.fadeOut == y.fadeOut && x.notes.size() == y.notes.size();
    });
}

TrackList::Change TrackList::applyContent(std::vector<Project::TrackContent> contents)
{
    std::erase_if(contents, [&](const Project::TrackContent& content) {
        const auto* track = m_project.findTrack(content.trackId);
        return !track || (sameClips(track->audioClips, content.clips) && track->volumeEnvelope.sameContent(content.envelope));
    });
    if (contents.empty()) return {true, false};
    const bool ok = m_project.replaceTrackContent(std::move(contents));
    return {ok, ok};
}

bool TrackList::copyRange(double startBeat, double endBeat)
{
    const auto from = sampleAtBeat(m_project.tempo(), startBeat);
    const auto to = sampleAtBeat(m_project.tempo(), endBeat);
    if (!from || !to || *to <= *from) return false;
    std::vector<ClipboardClip> clips;
    std::vector<ClipboardEnvelope> envelopes;
    int topRow = std::numeric_limits<int>::max();
    std::vector<int> clipRows, envelopeRows;
    const auto& tempo = m_project.tempo();
    for (int row = 0; row < count(); ++row) {
        const auto& track = *m_project.tracks()[row];
        for (const auto& clip : track.audioClips) {
            if (clip.start + clip.length <= *from || clip.start >= *to) continue;
            const auto piece = clipPiece(clip, *from, *to);
            ClipboardClip item;
            item.poolId = piece.poolId;
            item.beatOffset = tempo.beatAt(piece.start) - startBeat;
            item.lengthBeats = tempo.beatAt(piece.start + piece.length) - tempo.beatAt(piece.start);
            item.sourceOffset = piece.sourceOffset;
            item.fadeIn = piece.fadeIn;
            item.fadeOut = piece.fadeOut;
            item.gainDb = piece.gainDb;
            item.effectsPrinted = piece.effectsPrinted;
            item.reversed = piece.reversed;
            item.polarityInverted = piece.polarityInverted;
            item.fx = piece.fx;
            item.notes = piece.notes;
            clips.push_back(std::move(item));
            clipRows.push_back(row);
            topRow = std::min(topRow, row);
        }
        const auto& envelope = track.volumeEnvelope;
        if (envelope.points().empty()) continue;
        ClipboardEnvelope line;
        const auto add = [&](std::int64_t at, double level, automation::CurveShape shape) {
            line.points.push_back({tempo.beatAt(at) - startBeat, EnvelopePoint{0, level, shape}});
        };
        if (!pointAt(envelope.points(), *from)) add(*from, envelope.levelAt(*from), shapeThrough(envelope, *from));
        for (const auto& p : envelope.points()) if (p.position >= *from && p.position <= *to) add(p.position, p.level, p.shape);
        if (!pointAt(envelope.points(), *to)) add(*to, envelope.levelAt(*to), automation::CurveShape::Linear);
        envelopes.push_back(std::move(line));
        envelopeRows.push_back(row);
        topRow = std::min(topRow, row);
    }
    if (clips.empty() && envelopes.empty()) return false;
    for (std::size_t i = 0; i < clips.size(); ++i) clips[i].trackOffset = clipRows[i] - topRow;
    for (std::size_t i = 0; i < envelopes.size(); ++i) envelopes[i].trackOffset = envelopeRows[i] - topRow;
    m_clipboard = std::move(clips);
    m_clipboardEnvelopes = std::move(envelopes);
    m_clipboardLengthBeats = endBeat - startBeat;
    return true;
}

TrackList::Change TrackList::deleteRange(double startBeat, double endBeat, bool ripple)
{
    const StepName step(m_project, ripple ? "Ripple delete range" : "Delete range");
    if (editsLocked()) return {};
    const auto from = sampleAtBeat(m_project.tempo(), startBeat);
    const auto to = sampleAtBeat(m_project.tempo(), endBeat);
    if (!from || !to || *to <= *from) return {};
    std::vector<Project::TrackContent> contents;
    for (const auto& track : m_project.tracks())
        contents.push_back({track->id, clipsWithoutSpan(*track, *from, *to, ripple),
                            envelopeWithoutSpan(track->volumeEnvelope, *from, *to, ripple)});
    return applyContent(std::move(contents));
}

TrackList::Change TrackList::cutRange(double startBeat, double endBeat, bool ripple)
{
    const StepName step(m_project, ripple ? "Ripple cut range" : "Cut range");
    if (editsLocked() || !std::isfinite(startBeat) || !std::isfinite(endBeat) || endBeat <= startBeat) return {};
    const auto clipboard = m_clipboard;
    const auto envelopes = m_clipboardEnvelopes;
    const double length = m_clipboardLengthBeats;
    const bool copied = copyRange(startBeat, endBeat);
    const auto change = deleteRange(startBeat, endBeat, ripple);
    if (!change.accepted && copied) {   // nothing was cut: the clipboard stays as it was
        m_clipboard = clipboard; m_clipboardEnvelopes = envelopes; m_clipboardLengthBeats = length;
    }
    return change;
}

TrackList::Change TrackList::deleteClips(const std::vector<int>& ids, bool ripple)
{
    const StepName step(m_project, ripple ? (ids.size() == 1 ? "Ripple delete clip" : "Ripple delete clips") : (ids.size() == 1 ? "Delete clip" : "Delete clips"));
    if (!ripple) return editClips(ids, "delete", 0);
    if (editsLocked() || ids.empty()) return {};
    // Per track: the deleted clips go, and each span they covered closes up (later clips and
    // envelope points on that track move left), the latest span first.
    std::vector<Project::TrackContent> contents;
    bool any = false;
    for (const auto& track : m_project.tracks()) {
        std::vector<std::pair<std::int64_t, std::int64_t>> spans;
        Track working = *track;
        std::erase_if(working.audioClips, [&](const AudioClip& clip) {
            if (std::find(ids.begin(), ids.end(), clip.id) == ids.end()) return false;
            spans.push_back({clip.start, clip.start + clip.length});
            return true;
        });
        if (spans.empty()) continue;
        any = true;
        std::sort(spans.begin(), spans.end());
        std::vector<std::pair<std::int64_t, std::int64_t>> merged;
        for (const auto& span : spans) {
            if (!merged.empty() && span.first <= merged.back().second) merged.back().second = std::max(merged.back().second, span.second);
            else merged.push_back(span);
        }
        automation::VolumeEnvelope envelope = track->volumeEnvelope;
        for (auto it = merged.rbegin(); it != merged.rend(); ++it) {
            const auto [from, to] = *it;
            for (auto& clip : working.audioClips) if (clip.start >= to) clip.start -= to - from;
            envelope = envelopeWithoutSpan(envelope, from, to, true);
        }
        contents.push_back({track->id, std::move(working.audioClips), std::move(envelope)});
    }
    if (!any) return {};
    return applyContent(std::move(contents));
}

std::vector<int> TrackList::pasteWithContent(int targetRow, double targetBeat, bool ripple)
{
    const auto& tempo = m_project.tempo();
    const auto at = sampleAtBeat(tempo, targetBeat);
    const auto until = sampleAtBeat(tempo, targetBeat + std::max(0.0, m_clipboardLengthBeats));
    if (!at || !until) return {};
    const std::int64_t space = ripple ? *until - *at : 0;
    std::vector<Project::TrackContent> contents;
    for (const auto& track : m_project.tracks())
        contents.push_back({track->id, space > 0 ? clipsWithSpace(track->audioClips, *at, space) : track->audioClips,
                            space > 0 ? envelopeWithSpace(track->volumeEnvelope, *at, space) : track->volumeEnvelope});
    std::vector<int> added;
    for (const auto& item : m_clipboard) {
        const int row = std::clamp(targetRow + item.trackOffset, 0, count() - 1);
        const auto start = sampleAtBeat(tempo, targetBeat + item.beatOffset);
        const auto end = sampleAtBeat(tempo, targetBeat + item.beatOffset + item.lengthBeats);
        if (!start || !end || *end <= *start) continue;
        AudioClip clip;
        clip.id = m_project.takeClipId();
        clip.poolId = item.poolId;
        clip.start = *start;
        clip.length = *end - *start;
        clip.sourceOffset = item.sourceOffset;
        clip.fadeIn = item.fadeIn;
        clip.fadeOut = item.fadeOut;
        clip.gainDb = item.gainDb;
        clip.effectsPrinted = item.effectsPrinted;
        clip.reversed = item.reversed;
        clip.polarityInverted = item.polarityInverted;
        clip.fx = item.fx;
        clip.notes = item.notes;
        contents[std::size_t(row)].clips.push_back(std::move(clip));
        added.push_back(contents[std::size_t(row)].clips.back().id);
    }
    for (const auto& line : m_clipboardEnvelopes) {
        const int row = targetRow + line.trackOffset;
        if (row < 0 || row >= count()) continue;   // a line never lands on another track's envelope
        std::vector<automation::EnvelopePoint> points;
        for (const auto& [beat, point] : line.points)
            if (const auto position = sampleAtBeat(tempo, targetBeat + beat)) points.push_back({*position, point.level, point.shape});
        auto& envelope = contents[std::size_t(row)].envelope;
        envelope = envelopeWithPasted(envelope, *at, *until - *at, points);
    }
    const auto change = applyContent(std::move(contents));
    return change.changed ? added : std::vector<int>{};
}
