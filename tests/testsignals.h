#pragma once
#include "core/audioview.h"
#include "midi/midievent.h"
#include "processor/processor.h"
#include "rt/rtsafe.h"

#include <algorithm>
#include <cmath>
#include <vector>

// Signals to feed into DSP code, and ways to measure what comes out.
namespace testsignals {

constexpr double kSampleRate = 48000.0;
constexpr double kTwoPi = 6.283185307179586;

inline std::vector<float> sine(double frequency, double gainDb, int frames)
{
    const double gain = std::pow(10.0, gainDb / 20.0);
    std::vector<float> samples(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i)
        samples[static_cast<std::size_t>(i)] = static_cast<float>(gain * std::sin(kTwoPi * frequency * i / kSampleRate));
    return samples;
}

inline std::vector<float> impulse(int frames)
{
    std::vector<float> samples(static_cast<std::size_t>(frames), 0.0f);
    samples[0] = 1.0f;
    return samples;
}

inline double peakDb(const std::vector<float>& samples)
{
    float peak = 0.0f;
    for (float s : samples) peak = std::max(peak, std::abs(s));
    return 20.0 * std::log10(std::max(peak, 1e-12f));
}

inline double rmsDb(const std::vector<float>& samples)
{
    double sum = 0.0;
    for (float s : samples) sum += double(s) * s;
    return 10.0 * std::log10(std::max(sum / double(samples.size()), 1e-24));
}

// Calls process() under the audio thread rules. In the rt preset the program stops if it allocates or locks.
inline void rtProcess(Processor& processor, ProcessContext& context) noexcept RT_SAFE
{
    processor.process(context);
}

// Runs a processor over a mono signal in blocks. Use different block sizes: the result must not change.
inline std::vector<float> render(Processor& processor, std::vector<float> samples, int blockSize)
{
    processor.prepare(kSampleRate, 8192);
    processor.reset();

    MidiBuffer midi;
    const int total = static_cast<int>(samples.size());
    for (int done = 0; done < total; done += blockSize) {
        float* channels[1] = {samples.data() + done};
        ProcessContext context;
        context.audio = AudioView{channels, 1, std::min(blockSize, total - done)};
        context.midi = &midi;
        context.sampleRate = kSampleRate;
        context.samplePosition = done;
        rtProcess(processor, context);
    }
    return samples;
}

} // namespace testsignals
