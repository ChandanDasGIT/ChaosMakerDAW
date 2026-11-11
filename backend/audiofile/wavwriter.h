#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Writes a WAV file (restMap E2, F2). Float32 writes the layout the app's recordings use
// (format 3 with a fact chunk), which loadTakeWav reads back; Int16 and Int24 write plain PCM.
// Int16 adds TPDF dither (two uniform random values, one bit's worth) before rounding, so quiet
// fades do not turn into steps. Samples are clamped to -1..1 for the integer formats.
// Runs off the audio thread; throws std::runtime_error with a message a user can read.
namespace audiofile {

enum class WavFormat { Int16, Int24, Float32 };

// `channels`: 1 or 2 planar channels of the same length.
void writeWav(const std::string& path, const std::vector<std::vector<float>>& channels, double sampleRate,
              WavFormat format, std::uint32_t ditherSeed = 0x2545F491u);

} // namespace audiofile
