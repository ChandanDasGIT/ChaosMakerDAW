#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>

// A decoded file. Never changes after loading, so any thread may read it.
struct AudioData {
    std::vector<std::vector<float>> channels;
    double sampleRate = 48000.0;
    std::int64_t frames = 0;
    std::string path;
    using Peak = std::array<float, 2>;
    std::vector<Peak> peaks;
    static constexpr std::int64_t peakBlock = 256;
    void buildWaveform() {
        peaks.assign((std::max<std::int64_t>(0, frames) + peakBlock - 1) / peakBlock, Peak{0,0});
        for (const auto& channel : channels)
            for (std::size_t i = 0; i < channel.size() && i < std::size_t(frames); ++i) {
                const float v = std::isfinite(channel[i]) ? channel[i] : 0;
                auto& p = peaks[i / peakBlock]; p[0] = std::min(p[0], v); p[1] = std::max(p[1], v);
            }
    }
    std::vector<Peak> waveform(std::int64_t start, std::int64_t end, int bins) const {
        bins = std::clamp(bins, 1, 2048);
        std::vector<Peak> result(bins, Peak{0,0});
        if (end <= start) return result;
        for (int b = 0; b < bins; ++b) {
            auto from = std::clamp<std::int64_t>(start + (end-start) * double(b) / bins, 0, frames);
            const auto to = std::clamp<std::int64_t>(start + (end-start) * double(b+1) / bins, 0, frames);
            auto& p = result[b];
            while (from < to) {
                if (from % peakBlock == 0 && from + peakBlock <= to && from / peakBlock < std::int64_t(peaks.size())) {
                    const auto& cached = peaks[from / peakBlock];
                    p[0] = std::min(p[0], cached[0]); p[1] = std::max(p[1], cached[1]); from += peakBlock;
                } else {
                    for (const auto& channel : channels) if (from < std::int64_t(channel.size())) {
                        const float v = std::isfinite(channel[from]) ? channel[from] : 0;
                        p[0] = std::min(p[0], v); p[1] = std::max(p[1], v);
                    }
                    ++from;
                }
            }
        }
        return result;
    }
};
