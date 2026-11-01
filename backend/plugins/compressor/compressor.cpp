#include "compressor.h"
Compressor::Compressor() : controls{{&addParameter("threshold", "Threshold", -60.0f, 0.0f, -24.0f, "dB"),
        &addParameter("ratio", "Ratio", 1.0f, 20.0f, 4.0f, ":1"),
        &addParameter("attack", "Attack", 0.1f, 100.0f, 10.0f, "ms"),
        &addParameter("release", "Release", 10.0f, 1000.0f, 120.0f, "ms"),
        &addParameter("makeup", "Makeup", 0.0f, 24.0f, 0.0f, "dB")}} {}
void Compressor::prepare(double sampleRate, int) {
    rate = std::max(sampleRate, 1000.0);
    
    for (auto& smoother : smoothers) smoother.prepare(rate, 15.0f);
    reset();
}
void Compressor::reset() noexcept {
    for (std::size_t j = 0; j < controls.size(); ++j) smoothers[j].reset(controls[j]->get());
    reduction = 0;
}
void Compressor::process(ProcessContext& context) noexcept {
    const int channels = std::min(context.audio.channelCount, 2);
    for (int i = 0; i < context.audio.frames; ++i) {
        std::array<float, 5> v;
        for (std::size_t j = 0; j < controls.size(); ++j) v[j] = smoothers[j].next(controls[j]->get());

        const float db = 20.0f * std::log10(std::max(pedal::peak(context, i), 1e-9f));
        const float target = std::max(0.0f, db - v[0]) * (1.0f - 1.0f / v[1]);
        reduction += pedal::coefficient(rate, target > reduction ? v[2] : v[3]) * (target - reduction);
        const float amplitude = pedal::gain(v[4] - reduction);
        for (int c = 0; c < channels; ++c) context.audio.channel(c)[i] *= amplitude;

    }
}
