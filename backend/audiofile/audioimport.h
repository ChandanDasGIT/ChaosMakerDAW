#pragma once

#include "core/audiodata.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

// Reading audio files to import (restMap E1-E2): WAV, FLAC and MP3, recognised by their
// content (not the file name), mixed down to mono or stereo. Runs off the audio thread; throws
// std::runtime_error with a message a user can read.
namespace audioimport {

enum class Format { Wav, Flac, Mp3, Unknown };
Format detect(std::span<const std::uint8_t> head);
const char* formatName(Format format);

// The file's audio, 1 or 2 channels (more are mixed down: even channels left, odd right), at
// its own sample rate, path set, waveform built.
std::shared_ptr<AudioData> read(const std::string& path);

// Saves `audio` as a recording-style take (32-bit float WAV, which a reopened project reads)
// in `folder`, named after `baseName` and made unique; returns the file's path. The returned
// AudioData is `audio` with that path.
std::shared_ptr<AudioData> saveAsTake(const AudioData& audio, const std::string& folder, const std::string& baseName);

} // namespace audioimport
