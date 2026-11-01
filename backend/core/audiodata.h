#pragma once
#include <cstdint>
#include <string>
#include <vector>

// A decoded file. Never changes after loading, so any thread may read it.
struct AudioData {
    std::vector<std::vector<float>> channels;
    double sampleRate = 48000.0;
    std::int64_t frames = 0;
    std::string path;
};
