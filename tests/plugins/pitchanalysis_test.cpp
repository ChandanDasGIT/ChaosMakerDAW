#include "testkit.h"
#include "plugins/pitchmonitor/pitchanalysis.h"
#include "session/tracklist.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>

namespace {

std::shared_ptr<AudioData> sung(double hz, double seconds, const char* path)
{
    auto audio = std::make_shared<AudioData>();
    audio->path = path;
    audio->sampleRate = 48000;
    audio->frames = std::int64_t(seconds * 48000);
    audio->channels = {std::vector<float>(std::size_t(audio->frames))};
    for (std::size_t i = 0; i < audio->channels[0].size(); ++i) {
        const double t = double(i) / 48000.0;
        audio->channels[0][i] = float(0.2 * (std::sin(2 * std::numbers::pi * hz * t) + 0.4 * std::sin(4 * std::numbers::pi * hz * t)));
    }
    return audio;
}

// The median MIDI number of the voiced points between two beats (120 BPM, 48 kHz).
double midiBetween(const std::vector<pitchanalysis::Point>& points, double from, double to)
{
    std::vector<double> m;
    for (const auto& p : points)
        if (p.sample >= std::int64_t(from * 24000) && p.sample < std::int64_t(to * 24000) && p.midi > 0.0f) m.push_back(p.midi);
    if (m.empty()) return 0.0;
    std::sort(m.begin(), m.end());
    return m[m.size() / 2];
}

} // namespace

TEST(pitchanalysis, a_tracks_takes_become_one_line_in_song_time)
{
    TrackList tracks;                                           // 120 BPM, 48 kHz: a beat is 24000 samples
    const int vocal = tracks.addVocalTrack();
    CHECK(tracks.attachTake(vocal, 48000, sung(220.0, 1.0, "a3.wav"), false));     // beats 2..4, A3
    CHECK(tracks.attachTake(vocal, 240000, sung(261.63, 0.5, "c4.wav"), false));   // beats 10..11, C4

    const auto segments = pitchanalysis::collect(tracks, vocal);
    CHECK(segments.size() == 2);
    CHECK(segments.size() == 2 && segments[0].start == 48000 && segments[0].mono.size() == 48000);
    CHECK(segments.size() == 2 && segments[1].start == 240000);

    const auto points = pitchanalysis::analyse(segments, tracks.project().tempo().sampleRate());
    CHECK(!points.empty());
    for (std::size_t i = 1; i < points.size(); ++i) CHECK(points[i].sample >= points[i - 1].sample);
    CHECK_NEAR(midiBetween(points, 2.2, 3.8), 57.0, 0.05);
    CHECK_NEAR(midiBetween(points, 10.2, 10.8), 60.0, 0.05);
    CHECK(midiBetween(points, 4.2, 9.8) == 0.0);                // nothing between the takes
    // A gap point ends each take, so the line is not drawn across the silence.
    int gaps = 0;
    for (const auto& p : points) if (p.midi == 0.0f && p.sample > 93600 && p.sample < 98400) ++gaps;
    CHECK(gaps >= 1);

    // A clip's own monitor sees only that clip.
    const int second = tracks.project().findTrack(vocal)->audioClips[1].id;
    const auto own = pitchanalysis::collect(tracks, Project::clipEffectsId(second));
    CHECK(own.size() == 1 && own[0].start == 240000);

    // Trimming the take changes what is analysed.
    const int first = tracks.project().findTrack(vocal)->audioClips[0].id;
    CHECK(tracks.trimClipStartAtBeat(vocal, first, 3.0));
    const auto trimmed = pitchanalysis::collect(tracks, vocal);
    CHECK(trimmed.size() == 2 && trimmed[0].start == 72000 && trimmed[0].mono.size() == 24000);

    std::atomic<bool> cancel{true};
    CHECK(pitchanalysis::analyse(segments, 48000.0, &cancel).empty());
    CHECK(pitchanalysis::collect(tracks, -42).empty());
}
