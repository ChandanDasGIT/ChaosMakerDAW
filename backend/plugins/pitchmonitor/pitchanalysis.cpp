#include "plugins/pitchmonitor/pitchanalysis.h"

#include "session/tracklist.h"

#include <algorithm>
#include <utility>

namespace pitchanalysis {

std::vector<Segment> collect(const TrackList& tracks, int effectsId)
{
    const auto& project = tracks.project();
    std::vector<std::pair<std::int64_t, std::int64_t>> spans;
    const auto add = [&](const AudioClip& clip) {
        if (!tracks.isPatternClip(clip) && clip.length > 0) spans.emplace_back(clip.start, clip.start + clip.length);
    };
    if (const auto* clip = project.clipOfEffects(effectsId)) add(*clip);
    else if (const auto* track = project.findTrack(effectsId))
        for (const auto& clip : track->audioClips) add(clip);
    std::sort(spans.begin(), spans.end());
    std::vector<std::pair<std::int64_t, std::int64_t>> merged;
    for (const auto& span : spans) {
        if (!merged.empty() && span.first <= merged.back().second) merged.back().second = std::max(merged.back().second, span.second);
        else merged.push_back(span);
    }
    std::vector<Segment> segments;
    const auto piece = std::int64_t(MaxPieceSeconds * project.tempo().sampleRate());
    for (const auto& [from, to] : merged)
        for (std::int64_t at = from; at < to; at += piece) {
            auto mono = tracks.ownerAudio(effectsId, at, std::min(to, at + piece));
            if (!mono.empty()) segments.push_back({at, std::move(mono)});
        }
    return segments;
}

std::vector<Point> analyse(const std::vector<Segment>& segments, double sampleRate, const std::atomic<bool>* cancel)
{
    std::vector<Point> points;
    for (const auto& segment : segments) {
        const auto frames = pitch::Tracker::analyse(segment.mono, sampleRate, cancel);
        if (cancel && cancel->load(std::memory_order_relaxed)) return {};
        for (const auto& f : frames)
            points.push_back({segment.start + f.sample, f.hz > 0.0f ? float(pitch::midiOf(f.hz)) : 0.0f, f.clarity});
        if (!frames.empty()) points.push_back({segment.start + std::int64_t(segment.mono.size()), 0.0f, 0.0f});
    }
    return points;
}

} // namespace pitchanalysis
