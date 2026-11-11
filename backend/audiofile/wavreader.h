#pragma once
#include "core/audiodata.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

// Reads a whole WAV file: RIFF PCM 8/16/24/32-bit or IEEE float 32/64-bit, also in the
// extensible header, 1 to 8 channels, any sample rate from 1 kHz to 384 kHz. The first loop of
// a `smpl` chunk comes with it. Runs off the audio thread; throws std::runtime_error with a
// message a user can read.
//
// A data chunk that claims more bytes than the file holds (common in sample libraries) gives
// the whole frames that are there.
namespace audiofile {

struct WavFile {
    std::shared_ptr<AudioData> audio;     // frames, channels, rate and path set; no waveform yet
    int bitsPerSample = 0;
    bool isFloat = false;
    bool hasLoop = false;
    std::int64_t loopStart = 0, loopEnd = 0;   // frames, the end included (as in SFZ)
};

constexpr std::int64_t MaxWavBytes = 2LL * 1024 * 1024 * 1024;

WavFile readWav(const std::string& path);
// The same from bytes in memory; `path` only names the file in messages and in AudioData.
WavFile decodeWav(std::span<const std::uint8_t> bytes, const std::string& path);

} // namespace audiofile
