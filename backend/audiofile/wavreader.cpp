#include "audiofile/wavreader.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace audiofile {

namespace {

struct Reader {
    std::span<const std::uint8_t> b;
    std::uint32_t u16(std::size_t p) const { return std::uint32_t(b[p]) | (std::uint32_t(b[p + 1]) << 8); }
    std::uint32_t u32(std::size_t p) const { return u16(p) | (u16(p + 2) << 16); }
    bool tag(std::size_t p, const char* s) const { return std::memcmp(b.data() + p, s, 4) == 0; }
};

[[noreturn]] void fail(const std::string& path, const std::string& message)
{
    throw std::runtime_error(path + ": " + message);
}

// One sample at byte `p`, as -1..1.
double sampleAt(const Reader& r, std::size_t p, unsigned bits, bool isFloat)
{
    if (isFloat) {
        if (bits == 32) return std::bit_cast<float>(r.u32(p));
        return std::bit_cast<double>(std::uint64_t(r.u32(p)) | (std::uint64_t(r.u32(p + 4)) << 32));
    }
    switch (bits) {
    case 8: return (double(r.b[p]) - 128.0) / 128.0;              // 8-bit WAV is unsigned
    case 16: return std::bit_cast<std::int16_t>(std::uint16_t(r.u16(p))) / 32768.0;
    case 24: {
        const std::uint32_t n = r.u16(p) | (std::uint32_t(r.b[p + 2]) << 16);
        return (n >= 0x800000u ? double(n) - 16777216.0 : double(n)) / 8388608.0;
    }
    default: return std::bit_cast<std::int32_t>(r.u32(p)) / 2147483648.0;
    }
}

} // namespace

WavFile decodeWav(std::span<const std::uint8_t> bytes, const std::string& path)
{
    const Reader r{bytes};
    if (bytes.size() < 12 || !r.tag(0, "RIFF") || !r.tag(8, "WAVE")) fail(path, "not a WAV file (no RIFF/WAVE header).");
    // Trust the file length over the RIFF size: some writers leave it wrong.
    const std::size_t end = bytes.size();

    unsigned format = 0, channels = 0, rate = 0, align = 0, bits = 0;
    bool haveFormat = false;
    std::size_t data = 0, dataBytes = 0;
    bool haveData = false;
    WavFile out;
    for (std::size_t p = 12; p + 8 <= end;) {
        const std::size_t length = r.u32(p + 4), start = p + 8;
        const std::size_t available = std::min(length, end - start);
        if (r.tag(p, "fmt ")) {
            if (available < 16) fail(path, "the format chunk is too short.");
            format = r.u16(start); channels = r.u16(start + 2); rate = r.u32(start + 4);
            align = r.u16(start + 12); bits = r.u16(start + 14);
            if (format == 0xFFFE) {                                  // WAVE_FORMAT_EXTENSIBLE
                if (available < 40) fail(path, "the extensible format chunk is too short.");
                static constexpr std::uint8_t guidTail[]{0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00,
                                                         0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
                if (std::memcmp(bytes.data() + start + 26, guidTail, sizeof guidTail) != 0)
                    fail(path, "unsupported extensible WAV encoding.");
                format = r.u16(start + 24);
            }
            haveFormat = true;
        } else if (r.tag(p, "data") && !haveData) {
            data = start; dataBytes = available; haveData = true;
        } else if (r.tag(p, "smpl") && available >= 36 + 24 && r.u32(start + 28) > 0) {
            out.hasLoop = true;                                      // the first loop only
            out.loopStart = r.u32(start + 36 + 8);
            out.loopEnd = r.u32(start + 36 + 12);
        }
        if (length > end - start) break;                             // the last chunk, cut short
        p = start + length + (length & 1u);
    }

    if (!haveFormat) fail(path, "no format chunk.");
    if (!haveData) fail(path, "no audio data chunk.");
    const bool isFloat = format == 3;
    if (format != 1 && format != 3) fail(path, "only PCM and float WAV can be read (format " + std::to_string(format) + ").");
    if (isFloat ? (bits != 32 && bits != 64) : (bits != 8 && bits != 16 && bits != 24 && bits != 32))
        fail(path, std::to_string(bits) + "-bit " + (isFloat ? "float" : "PCM") + " WAV is not supported.");
    if (channels != 1 && channels != 2) fail(path, std::to_string(channels) + " channels: only mono and stereo are supported.");
    if (rate < 1000 || rate > 384000) fail(path, "sample rate " + std::to_string(rate) + " Hz is out of range.");
    if (align != channels * (bits / 8)) fail(path, "the block size does not match the format.");

    const std::size_t frames = dataBytes / align;
    auto audio = std::make_shared<AudioData>();
    audio->path = path;
    audio->sampleRate = rate;
    audio->frames = std::int64_t(frames);
    audio->channels.assign(channels, std::vector<float>(frames));
    for (std::size_t i = 0; i < frames; ++i) {
        for (unsigned c = 0; c < channels; ++c) {
            const double v = sampleAt(r, data + i * align + c * (bits / 8), bits, isFloat);
            if (!std::isfinite(v)) fail(path, "contains invalid (NaN or infinite) samples.");
            audio->channels[c][i] = float(v);
        }
    }
    out.audio = std::move(audio);
    out.bitsPerSample = int(bits);
    out.isFloat = isFloat;
    if (out.hasLoop && (out.loopEnd <= out.loopStart || out.loopEnd >= std::int64_t(frames))) out.hasLoop = false;
    if (!out.hasLoop) out.loopStart = out.loopEnd = 0;
    return out;
}

WavFile readWav(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) fail(path, "the file cannot be opened.");
    const auto size = std::int64_t(file.tellg());
    if (size <= 0) fail(path, "the file is empty.");
    if (size > MaxWavBytes) fail(path, "the file is larger than 2 GB.");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) fail(path, "the file cannot be read.");
    return decodeWav(bytes, path);
}

} // namespace audiofile
