#include "chorus.h"
Chorus::Chorus() : controls{{&addParameter("rate", "Rate", 0.05f, 5.0f, 0.8f, "Hz"),
        &addParameter("depth", "Depth", 0.0f, 1.0f, 0.5f, ""),
        &addParameter("mix", "Mix", 0.0f, 1.0f, 0.3f, "")}} {}
void Chorus::prepare(double sampleRate, int) {
    rate = std::max(sampleRate, 1000.0);
    for (auto& line : lines) line.prepare(int(rate * 0.025) + 2);
    for (auto& smoother : smoothers) smoother.prepare(rate, 15.0f);
    reset();
}
void Chorus::reset() noexcept {
    for (std::size_t j = 0; j < controls.size(); ++j) smoothers[j].reset(controls[j]->get());
    phase = 0; for (auto& line : lines) line.reset();
}
void Chorus::process(ProcessContext& context) noexcept {
    const int channels = std::min(context.audio.channelCount, 2);
    for (int i = 0; i < context.audio.frames; ++i) {
        std::array<float, 3> v;
        for (std::size_t j = 0; j < controls.size(); ++j) v[j] = smoothers[j].next(controls[j]->get());

        for (int c = 0; c < channels; ++c) {
            float& sample = context.audio.channel(c)[i];
            const float ms = 15.0f + 6.0f * v[1] * std::sin(float(phase) + c * pedal::pi * 0.5f);
            const float delayed = lines[c].read(ms * float(rate * 0.001));
            lines[c].write(sample);
            sample = sample * (1.0f - v[2]) + delayed * v[2];
        }
        phase += 2.0 * pedal::pi * v[0] / rate;
        if (phase >= 2.0 * pedal::pi) phase -= 2.0 * pedal::pi;

    }
}
