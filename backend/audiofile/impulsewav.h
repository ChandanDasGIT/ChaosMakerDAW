#pragma once
#include <array>
#include <string>
#include <vector>
struct CabinetImpulse {
    std::array<std::vector<float>,2> channels;
    int sampleRate=0;
};
// Worker-thread WAV decoding and resampling. Throws with a user-readable error.
// RIFF PCM16/24/32 or IEEE float32/64, mono/stereo; first 200 ms, max 32 MB file.
CabinetImpulse loadImpulseWav(const std::string& path,double targetRate);
