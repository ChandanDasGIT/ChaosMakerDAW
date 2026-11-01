#pragma once
#include <algorithm>

// Audio that someone else owns. Cheap to copy, safe on the audio thread.
struct AudioView {
    float* const* channels = nullptr;
    int channelCount = 0;
    int frames = 0;

    float* channel(int index) const noexcept { return channels[index]; }
    void clear() const noexcept {
        for (int c = 0; c < channelCount; ++c) std::fill_n(channels[c], frames, 0.0f);
    }
};
