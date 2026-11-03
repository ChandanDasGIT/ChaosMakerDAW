#include "clipwaveform.h"

#include <cmath>
#include <limits>

std::vector<AudioData::Peak> clipWaveform(const AudioClip& clip, const AudioData& audio,
                                         double projectRate, int bins, double from, double to)
{
    if (!std::isfinite(from) || !std::isfinite(to) || from < 0 || to > 1 || to <= from
        || !std::isfinite(projectRate) || projectRate <= 0
        || !std::isfinite(audio.sampleRate) || audio.sampleRate <= 0
        || clip.length <= 0 || clip.sourceOffset < 0 || clip.sourceOffset >= audio.frames)
        return {};
    const double length = clip.length * audio.sampleRate / projectRate;
    const double start = length * from, end = length * to;
    if (!std::isfinite(end) || end >= double(INT64_MAX - clip.sourceOffset)) return {};
    return audio.waveform(clip.sourceOffset + static_cast<std::int64_t>(start),
                           clip.sourceOffset + static_cast<std::int64_t>(end), bins);
}
