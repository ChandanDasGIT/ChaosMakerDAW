#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Writes an MP3 file (MPEG-1 Audio Layer III, constant bitrate), written from the standard
// (ISO/IEC 11172-3), no library (restMap F2: MP3 export). The polyphase analysis filterbank,
// the MDCT, a psychoacoustic model (band energies spread across neighbouring bands, the
// threshold of hearing), the quantization loops (global gain to fit the bits, scale factors to
// keep each band's noise under its threshold when the bits allow), Huffman coding with the
// cheapest table per region, mid/side stereo when the channels are alike, the bit reservoir,
// and an Info header with a LAME-style tag carrying the encoder delay and padding, so decoders
// that read it (this app's, ffmpeg, most players) give back exactly the samples put in.
// Long blocks only (no block switching): very sharp attacks can pre-echo at low bitrates.
// 32, 44.1 and 48 kHz are encoded as they are; other rates are resampled to 44.1 or 48 kHz.
// Runs off the audio thread; throws std::runtime_error with a message a user can read.
namespace audiofile {

struct Mp3Options {
    int bitrateKbps = 320;   // 32 .. 320, one of the MPEG-1 Layer III rates
};

// `channels`: 1 or 2 planar channels of the same length, any sample rate 8 kHz .. 192 kHz.
void writeMp3(const std::string& path, const std::vector<std::vector<float>>& channels, double sampleRate,
              const Mp3Options& options = {});
// The same into memory; returns the file's bytes.
std::vector<std::uint8_t> encodeMp3(const std::vector<std::vector<float>>& channels, double sampleRate,
                                    const Mp3Options& options = {});

} // namespace audiofile
