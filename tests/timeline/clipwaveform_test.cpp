#include "testkit.h"
#include "timeline/clipwaveform.h"

#include <limits>

TEST(clipwaveform, visible_interval_uses_source_rate_offset_and_cached_peaks)
{
    AudioData audio; audio.sampleRate = 24000; audio.frames = 1024;
    audio.channels = {std::vector<float>(1024)};
    audio.channels[0][768] = 0.8f; audio.buildWaveform();
    AudioClip clip; clip.length = 1024; clip.sourceOffset = 512;
    const auto full = clipWaveform(clip, audio, 48000, 2);
    CHECK(full.size() == 2);
    CHECK(full[0][1] == 0); CHECK(full[1][1] == 0.8f);
    const auto visible = clipWaveform(clip, audio, 48000, 1, 0.5, 1);
    CHECK(visible.size() == 1 && visible[0][1] == 0.8f);
    CHECK(clipWaveform(clip, audio, 48000, 0).size() == 1);
}

TEST(clipwaveform, malformed_intervals_rates_and_offsets_do_not_read_audio)
{
    AudioData audio; audio.frames = 1024; audio.channels = {std::vector<float>(1024)};
    AudioClip clip; clip.length = 1024;
    CHECK(clipWaveform(clip, audio, 48000, 2, -0.1, 1).empty());
    CHECK(clipWaveform(clip, audio, 48000, 2, 0.5, 0.5).empty());
    CHECK(clipWaveform(clip, audio, 48000, 2, 0, 1.1).empty());
    CHECK(clipWaveform(clip, audio, 0, 2).empty());
    CHECK(clipWaveform(clip, audio, std::numeric_limits<double>::quiet_NaN(), 2).empty());
    clip.sourceOffset = audio.frames;
    CHECK(clipWaveform(clip, audio, 48000, 2).empty());
}
