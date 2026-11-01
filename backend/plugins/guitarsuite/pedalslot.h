#pragma once

#include "processor/processor.h"

#include <atomic>
#include <memory>
#include <string>

// Shared UI/audio state for a pedal slot. The processor's parameters are
// atomic; the chain snapshot owns this state until the audio thread retires it.
struct PedalSlot {
    int id = 0;
    std::string type;
    std::string resourcePath; // Cabinet WAV reference; populated off the audio thread.
    std::shared_ptr<Processor> processor;
    std::atomic<bool> bypassed{false};
    double preparedSampleRate = 0.0;
    int preparedMaxFrames = 0;
};
