#pragma once
#include "core/audiodata.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

// Reads a whole MP3 file (MPEG-1, MPEG-2 and MPEG-2.5 Audio Layer III), written from the
// standard (ISO/IEC 11172-3 and 13818-3), no library: frame sync, the bit reservoir, scale
// factors, Huffman decoding, requantization, mid/side and intensity stereo, alias reduction,
// the IMDCT and the polyphase synthesis filterbank. A leading ID3v2 tag and trailing tags are
// skipped. A Xing / Info header with a LAME tag gives the encoder delay and padding, which are
// trimmed so the audio starts and ends where it was encoded (gapless). Runs off the audio
// thread; throws std::runtime_error with a message a user can read (restMap E: MP3).
namespace audiofile {

struct Mp3File {
    std::shared_ptr<AudioData> audio;     // frames, channels, rate and path set; no waveform yet
    int version = 1;                      // 1 = MPEG-1, 2 = MPEG-2, 25 = MPEG-2.5
    int bitrateKbps = 0;                  // the average over the frames
    bool variableBitrate = false;
    int encoderDelay = -1;                // from the LAME tag; -1 without one
    int encoderPadding = -1;
};

constexpr std::int64_t MaxMp3Bytes = 1024LL * 1024 * 1024;

Mp3File readMp3(const std::string& path);
// The same from bytes in memory; `path` only names the file in messages and in AudioData.
Mp3File decodeMp3(std::span<const std::uint8_t> bytes, const std::string& path);

} // namespace audiofile
