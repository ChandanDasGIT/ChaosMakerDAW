#pragma once
#include "core/audiodata.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

// Reads a whole FLAC file, written from the format description (no library): the stream
// header (STREAMINFO), then every frame with its subframes (constant, verbatim, fixed and LPC
// prediction), Rice-coded residuals, the stereo decorrelation modes and wasted bits. 4 to 32
// bits per sample, 1 to 8 channels, any rate. Each frame's CRC-8 and CRC-16 are checked, and
// the decoded samples against the MD5 the file carries (when it carries one). A leading ID3v2
// tag is skipped. Runs off the audio thread; throws std::runtime_error with a message a user
// can read.
namespace audiofile {

struct FlacFile {
    std::shared_ptr<AudioData> audio;     // frames, channels, rate and path set; no waveform yet
    int bitsPerSample = 0;
    bool md5Checked = false;              // false when the file carries no MD5 (all zero)
};

constexpr std::int64_t MaxFlacBytes = 2LL * 1024 * 1024 * 1024;

FlacFile readFlac(const std::string& path);
// The same from bytes in memory; `path` only names the file in messages and in AudioData.
FlacFile decodeFlac(std::span<const std::uint8_t> bytes, const std::string& path);

} // namespace audiofile
