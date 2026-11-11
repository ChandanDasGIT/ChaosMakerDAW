#include "audiofile/audioimport.h"
#include "audiofile/flacreader.h"
#include "audiofile/mp3reader.h"
#include "audiofile/wavreader.h"
#include "audiofile/wavwriter.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace audioimport {

Format detect(std::span<const std::uint8_t> h)
{
    const auto starts = [&](const char* tag, std::size_t at = 0) {
        const std::size_t n = std::strlen(tag);
        return h.size() >= at + n && std::memcmp(h.data() + at, tag, n) == 0;
    };
    if (starts("RIFF") && starts("WAVE", 8)) return Format::Wav;
    if (starts("fLaC")) return Format::Flac;
    if (starts("ID3")) {
        // An ID3v2 tag can sit in front of FLAC or MP3: look behind it.
        if (h.size() < 10) return Format::Unknown;
        const std::size_t size = (std::size_t(h[6] & 0x7f) << 21) | (std::size_t(h[7] & 0x7f) << 14)
                               | (std::size_t(h[8] & 0x7f) << 7) | std::size_t(h[9] & 0x7f);
        const std::size_t after = 10 + size + ((h[5] & 0x10) ? 10 : 0);
        if (after + 4 <= h.size()) {
            if (std::memcmp(h.data() + after, "fLaC", 4) == 0) return Format::Flac;
            if (h[after] == 0xFF && (h[after + 1] & 0xE0) == 0xE0) return Format::Mp3;
        }
        return Format::Mp3;   // the tag is longer than what was read: MP3 is by far the likeliest
    }
    if (h.size() >= 2 && h[0] == 0xFF && (h[1] & 0xE6) == 0xE2) return Format::Mp3;   // Layer III sync
    return Format::Unknown;
}

const char* formatName(Format format)
{
    switch (format) {
    case Format::Wav: return "WAV";
    case Format::Flac: return "FLAC";
    case Format::Mp3: return "MP3";
    default: return "unknown";
    }
}

namespace {

// More than two channels: even channels mixed to the left, odd to the right.
void mixDownToStereo(AudioData& audio)
{
    if (audio.channels.size() <= 2) return;
    std::vector<std::vector<float>> stereo(2, std::vector<float>(std::size_t(audio.frames), 0.0f));
    std::array<int, 2> counts{0, 0};
    for (std::size_t c = 0; c < audio.channels.size(); ++c) {
        const std::size_t side = c % 2;
        ++counts[side];
        for (std::size_t i = 0; i < std::size_t(audio.frames) && i < audio.channels[c].size(); ++i)
            stereo[side][i] += audio.channels[c][i];
    }
    for (std::size_t side = 0; side < 2; ++side)
        for (auto& v : stereo[side]) v /= float(std::max(1, counts[side]));
    audio.channels = std::move(stereo);
}

std::vector<std::uint8_t> head(const std::string& path, std::size_t bytes)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open " + std::filesystem::path(path).filename().string() + ".");
    std::vector<std::uint8_t> data(bytes);
    file.read(reinterpret_cast<char*>(data.data()), std::streamsize(bytes));
    data.resize(std::size_t(file.gcount()));
    return data;
}

} // namespace

std::shared_ptr<AudioData> read(const std::string& path)
{
    const std::string name = std::filesystem::path(path).filename().string();
    std::shared_ptr<AudioData> audio;
    switch (detect(head(path, 4096))) {
    case Format::Wav: audio = audiofile::readWav(path).audio; break;
    case Format::Flac: audio = audiofile::readFlac(path).audio; break;
    case Format::Mp3: audio = audiofile::readMp3(path).audio; break;
    default: throw std::runtime_error(name + " is not a WAV, FLAC or MP3 file.");
    }
    if (!audio || audio->frames <= 0 || audio->channels.empty()) throw std::runtime_error(name + " has no audio.");
    mixDownToStereo(*audio);
    audio->path = path;
    audio->buildWaveform();
    return audio;
}

std::shared_ptr<AudioData> saveAsTake(const AudioData& audio, const std::string& folder, const std::string& baseName)
{
    std::filesystem::create_directories(folder);
    std::string stem = std::filesystem::path(baseName).stem().string();
    for (char& c : stem) if (c == '/' || c == '\\' || c == '\0') c = '_';
    if (stem.empty()) stem = "import";
    std::filesystem::path target = std::filesystem::path(folder) / ("import-" + stem + ".wav");
    for (int n = 2; std::filesystem::exists(target); ++n)
        target = std::filesystem::path(folder) / ("import-" + stem + "-" + std::to_string(n) + ".wav");
    audiofile::writeWav(target.string(), audio.channels, audio.sampleRate, audiofile::WavFormat::Float32);
    auto copy = std::make_shared<AudioData>(audio);
    copy->path = target.string();
    if (copy->peaks.empty()) copy->buildWaveform();
    return copy;
}

} // namespace audioimport
