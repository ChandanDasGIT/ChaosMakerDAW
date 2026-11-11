#include "audiofile/wavwriter.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace audiofile {
namespace {

void u16(std::vector<char>& out, std::uint32_t n) { out.push_back(char(n)); out.push_back(char(n >> 8)); }
void u32(std::vector<char>& out, std::uint32_t n) { for (int i = 0; i < 4; ++i) out.push_back(char(n >> (8 * i))); }

// A small, fast random source for dither (xorshift32).
struct Random {
    std::uint32_t state;
    float next() noexcept
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return float(state) / 4294967296.0f;   // 0..1
    }
};

} // namespace

void writeWav(const std::string& path, const std::vector<std::vector<float>>& channels, double sampleRate,
              WavFormat format, std::uint32_t ditherSeed)
{
    if (channels.empty() || channels.size() > 2) throw std::runtime_error("A WAV export needs one or two channels.");
    const std::size_t frames = channels.front().size();
    for (const auto& c : channels) if (c.size() != frames) throw std::runtime_error("Channels of different lengths.");
    if (!std::isfinite(sampleRate) || sampleRate < 1000 || sampleRate > 768000) throw std::runtime_error("Unsupported sample rate.");
    const unsigned channelCount = unsigned(channels.size());
    const unsigned bytesPerSample = format == WavFormat::Int16 ? 2 : format == WavFormat::Int24 ? 3 : 4;
    const std::uint64_t dataBytes = std::uint64_t(frames) * channelCount * bytesPerSample;
    if (dataBytes > 0xFFFFFF00ull) throw std::runtime_error("The file would be larger than a WAV can hold (4 GB).");
    const unsigned rate = unsigned(std::lround(sampleRate));
    const bool isFloat = format == WavFormat::Float32;

    std::vector<char> header;
    header.insert(header.end(), {'R', 'I', 'F', 'F'});
    u32(header, std::uint32_t((isFloat ? 48 : 36) + dataBytes));
    header.insert(header.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    u32(header, 16);
    u16(header, isFloat ? 3 : 1);
    u16(header, channelCount);
    u32(header, rate);
    u32(header, rate * channelCount * bytesPerSample);
    u16(header, channelCount * bytesPerSample);
    u16(header, bytesPerSample * 8);
    if (isFloat) { header.insert(header.end(), {'f', 'a', 'c', 't'}); u32(header, 4); u32(header, std::uint32_t(frames)); }
    header.insert(header.end(), {'d', 'a', 't', 'a'});
    u32(header, std::uint32_t(dataBytes));

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("Cannot write " + path + ".");
    file.write(header.data(), std::streamsize(header.size()));
    Random random{ditherSeed ? ditherSeed : 1u};
    std::vector<char> block;
    block.reserve(65536);
    for (std::size_t f = 0; f < frames; ++f) {
        for (unsigned c = 0; c < channelCount; ++c) {
            float v = channels[c][f];
            if (!std::isfinite(v)) v = 0.0f;
            if (isFloat) {
                u32(block, std::bit_cast<std::uint32_t>(v));
            } else if (format == WavFormat::Int16) {
                const float dither = random.next() - random.next();   // TPDF, +-1 LSB
                const long s = std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f + dither);
                u16(block, std::uint32_t(std::clamp<long>(s, -32768, 32767)) & 0xFFFF);
            } else {
                const long s = std::lround(double(std::clamp(v, -1.0f, 1.0f)) * 8388607.0);
                const std::uint32_t u = std::uint32_t(std::clamp<long>(s, -8388608, 8388607)) & 0xFFFFFF;
                block.push_back(char(u)); block.push_back(char(u >> 8)); block.push_back(char(u >> 16));
            }
        }
        if (block.size() >= 60000) { file.write(block.data(), std::streamsize(block.size())); block.clear(); }
    }
    file.write(block.data(), std::streamsize(block.size()));
    file.flush();
    if (!file) throw std::runtime_error("Writing " + path + " failed; check the disk space.");
}

} // namespace audiofile
