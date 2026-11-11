#include "render/songrender.h"
#include "engine/audioengine.h"
#include "engine/enginecommands.h"

#include <algorithm>
#include <cmath>

namespace songrender {

std::vector<std::vector<float>> render(std::unique_ptr<EngineSnapshot> snapshot, const Options& o,
                                       const std::function<bool(double)>& progress)
{
    std::vector<std::vector<float>> out(2);
    const std::int64_t total = std::max<std::int64_t>(0, o.length) + std::max<std::int64_t>(0, o.tail);
    if (!snapshot || total <= 0 || o.sampleRate <= 0) return out;
    const int block = std::clamp(o.blockFrames, 32, 1024);
    AudioEngine engine{nullptr};
    engine.deviceChanged(o.sampleRate, block);
    engine.setTempoBpm(o.bpm);
    engine.setMasterVolume(o.masterVolume);
    engine.setMasterMuted(o.masterMuted);
    engine.publishPlayback(std::move(snapshot));
    // Play through the end: the tail is rendered whatever the "stop at the end" setting says.
    engine.post({EngineCommand::Type::SetStopAtEnd, 0});
    engine.post({EngineCommand::Type::Seek, std::max<std::int64_t>(0, o.start)});
    engine.post({EngineCommand::Type::Play});
    for (auto& c : out) c.reserve(std::size_t(total));
    std::vector<float> left(static_cast<std::size_t>(block)), right(static_cast<std::size_t>(block));
    float* channels[]{left.data(), right.data()};
    std::int64_t done = 0, nextReport = 0;
    while (done < total) {
        const int frames = int(std::min<std::int64_t>(block, total - done));
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        engine.renderBlock({{nullptr, 0, 0}, {channels, 2, frames}, nullptr, frames});
        out[0].insert(out[0].end(), left.begin(), left.begin() + frames);
        out[1].insert(out[1].end(), right.begin(), right.begin() + frames);
        done += frames;
        if (progress && done >= nextReport) {
            if (!progress(double(done) / double(total))) { engine.stop(); return std::vector<std::vector<float>>(2); }
            nextReport = done + std::int64_t(o.sampleRate / 4);
        }
    }
    engine.stop();
    return out;
}

void normalise(std::vector<std::vector<float>>& channels, float peakDb)
{
    float peak = 0.0f;
    for (const auto& c : channels) for (float v : c) if (std::isfinite(v)) peak = std::max(peak, std::abs(v));
    if (peak <= 0.0f) return;
    const float gain = std::pow(10.0f, peakDb / 20.0f) / peak;
    for (auto& c : channels) for (float& v : c) v = std::isfinite(v) ? v * gain : 0.0f;
}

} // namespace songrender
