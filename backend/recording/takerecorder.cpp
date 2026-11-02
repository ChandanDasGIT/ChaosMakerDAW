#include "takerecorder.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
void u32(std::ostream& out, std::uint32_t n) {
    const char b[]{char(n), char(n >> 8), char(n >> 16), char(n >> 24)};
    out.write(b, 4);
}
void header(std::ostream& out, unsigned rate, unsigned frames, unsigned channels) {
    const unsigned bytesPerFrame = channels * 4;
    out.seekp(0); out.write("RIFF", 4); u32(out, 48 + frames * bytesPerFrame);
    out.write("WAVEfmt ", 8); u32(out, 16);
    u32(out, 3 | (channels << 16)); u32(out, rate); u32(out, rate * bytesPerFrame);
    u32(out, bytesPerFrame | (32 << 16)); out.write("fact", 4); u32(out, 4); u32(out, frames);
    out.write("data", 4); u32(out, frames * bytesPerFrame);
}
unsigned read32(const unsigned char* p) { return unsigned(p[0]) | unsigned(p[1]) << 8 | unsigned(p[2]) << 16 | unsigned(p[3]) << 24; }
}
TakeRecorder::TakeRecorder(const std::string& path, double sampleRate, Source source)
    : m_source(source), m_channels(source == Source::PostFx ? 2 : 1), file(path, std::ios::binary | std::ios::trunc), result(std::make_shared<AudioData>()) {
    if (!file || !std::isfinite(sampleRate) || sampleRate < 1000 || sampleRate > 768000)
        throw std::runtime_error("Cannot create recording WAV: " + path);
    result->path = path; result->sampleRate = sampleRate; result->channels.resize(m_channels);
    header(file, unsigned(sampleRate), 0, m_channels);
    if (!file) throw std::runtime_error("Cannot write recording WAV header.");
    worker = std::thread([this] { run(); });
}
TakeRecorder::~TakeRecorder() { finish(); wait(); }
void TakeRecorder::wait() { if (worker.joinable()) worker.join(); }
bool TakeRecorder::capture(const float* input, int frames) noexcept {
    if (frames <= 0) return true;
    if (m_channels != 1 || inputFinished.load(std::memory_order_acquire)) return false;
    if (!ring.push({input, static_cast<std::size_t>(frames)})) {
        overflow.store(true, std::memory_order_relaxed); finish(); return false;
    }
    return true;
}
bool TakeRecorder::capture(const AudioView& audio) noexcept {
    if (audio.frames <= 0) return true;
    if (audio.channelCount != int(m_channels) || inputFinished.load(std::memory_order_acquire)) return false;
    if (!ring.pushPlanar(audio.channels, m_channels, static_cast<std::size_t>(audio.frames))) {
        overflow.store(true, std::memory_order_relaxed); finish(); return false;
    }
    return true;
}
void TakeRecorder::run() {
    std::array<float, 4096> buffer;
    try {
        while (true) {
            // Acquire completion before inspecting the ring: the final push precedes finish().
            const bool finished = inputFinished.load(std::memory_order_acquire);
            const auto count = std::min(buffer.size(), ring.availableToRead());
            if (!count) {
                if (finished) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue;
            }
            if (!ring.pop({buffer.data(), count})) continue;
            // In-memory playback is intentionally bounded (one hour at 48 kHz).
            if (result->frames + std::int64_t(count / m_channels) > std::min<std::int64_t>(std::int64_t(result->sampleRate * 3600), (1024LL * 1024 * 1024 - 56) / (4 * m_channels)))
                throw std::runtime_error("Take reached the one-hour recording limit.");
            for (std::size_t i = 0; i < count; ++i) {
                if (!std::isfinite(buffer[i])) buffer[i] = 0;
                u32(file, std::bit_cast<std::uint32_t>(buffer[i]));
            }
            if (!file) throw std::runtime_error("Disk write failed; recording stopped.");
            for (std::size_t i = 0; i < count; i += m_channels)
                for (unsigned c = 0; c < m_channels; ++c)
                    result->channels[c].push_back(buffer[i + c]);
            result->frames += std::int64_t(count / m_channels);
        }
        result->buildWaveform();
        if (overflow.load(std::memory_order_relaxed)) failure = "Recording buffer overflow; take stopped at the last complete block.";
    } catch (const std::exception& e) { failure = e.what(); }
    inputFinished.store(true, std::memory_order_release);
    header(file, unsigned(result->sampleRate), unsigned(result->frames), m_channels); file.flush();
    if (!file) failure = "Could not finalize the recording WAV; check disk space.";
    file.close();
    completed.store(true, std::memory_order_release);
}
std::shared_ptr<const AudioData> loadTakeWav(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (!file || size < 56 || size > 1024LL * 1024 * 1024) throw std::runtime_error("Missing or invalid take WAV: " + path);
    file.seekg(0); std::array<unsigned char, 56> h{}; file.read(reinterpret_cast<char*>(h.data()), h.size());
    const unsigned rate = read32(h.data() + 24), bytes = read32(h.data() + 52);
    const unsigned channels = read32(h.data() + 20) >> 16;
    const unsigned bytesPerFrame = channels * 4;
    if (std::memcmp(h.data(), "RIFF", 4) || std::memcmp(h.data()+8, "WAVEfmt ", 8)
        || read32(h.data()+16) != 16 || (channels != 1 && channels != 2)
        || (read32(h.data()+20) & 0xffff) != 3
        || read32(h.data()+32) != (0x200000 | bytesPerFrame)
        || read32(h.data()+28) != rate * bytesPerFrame || std::memcmp(h.data()+36, "fact", 4)
        || std::memcmp(h.data()+48, "data", 4) || bytes % bytesPerFrame || std::int64_t(bytes)+56 != size
        || read32(h.data()+4) != bytes + 48 || read32(h.data()+40) != 4
        || read32(h.data()+44) != bytes / bytesPerFrame
        || rate < 1000 || rate > 768000)
        throw std::runtime_error("Unsupported or damaged take WAV: " + path);
    auto audio = std::make_shared<AudioData>(); audio->sampleRate = rate; audio->path = path;
    audio->frames = bytes / bytesPerFrame;
    audio->channels.resize(channels);
    for (auto& channel : audio->channels) channel.resize(audio->frames);
    for (std::int64_t i = 0; i < audio->frames; ++i) for (auto& channel : audio->channels) {
        float& sample = channel[i];
        unsigned char b[4]; file.read(reinterpret_cast<char*>(b), 4);
        if (!file) throw std::runtime_error("Truncated take WAV: " + path);
        sample = std::bit_cast<float>(read32(b));
        if (!std::isfinite(sample)) sample = 0;
    }
    audio->buildWaveform();
    return audio;
}
