#include "wah.h"
Wah::Wah() : controls{{&addParameter("position", "Position", 0.0f, 1.0f, 0.5f, ""),
        &addParameter("resonance", "Resonance", 0.5f, 5.0f, 2.0f, ""),
        &addParameter("mix", "Mix", 0.0f, 1.0f, 1.0f, "")}} {}
void Wah::prepare(double sampleRate, int) {
    rate = std::max(sampleRate, 1000.0);
    
    for (auto& smoother : smoothers) smoother.prepare(rate, 15.0f);
    reset();
}
void Wah::reset() noexcept {
    for (std::size_t j = 0; j < controls.size(); ++j) smoothers[j].reset(controls[j]->get());
    ic1.fill(0); ic2.fill(0);
}
void Wah::process(ProcessContext& context) noexcept {
    const int channels = std::min(context.audio.channelCount, 2);
    for (int i = 0; i < context.audio.frames; ++i) {
        std::array<float, 3> v;
        for (std::size_t j = 0; j < controls.size(); ++j) v[j] = smoothers[j].next(controls[j]->get());

        // Topology-preserving state variable band-pass, swept logarithmically.
        const float frequency = std::min(350.0f * std::pow(8.0f, v[0]), float(rate * 0.4));
        const float g = std::tan(pedal::pi * frequency / float(rate));
        const float k = 1.0f / v[1];
        const float a1 = 1.0f / (1.0f + g * (g + k));
        for (int c = 0; c < channels; ++c) {
            float& sample = context.audio.channel(c)[i];
            const float v1 = a1 * (ic1[c] + g * (sample - ic2[c]));
            const float v2 = ic2[c] + g * v1;
            ic1[c] = 2.0f * v1 - ic1[c]; ic2[c] = 2.0f * v2 - ic2[c];
            // A resonant peak keeps the broadband chain signal intact. Replacing
            // it with band-pass alone discarded lows, highs and upstream tails.
            sample += 2.0f * k * v1 * v[2];
        }

    }
}
