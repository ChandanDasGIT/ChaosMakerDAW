#include "fuzz.h"

Fuzz::Fuzz() : controls{{
    &addParameter("fuzz", "Fuzz", 0, 36, 24, "dB"),
    &addParameter("sustain", "Sustain", 0, 1, 0.65f),
    &addParameter("tone", "Tone", 0, 1, 0.5f),
    &addParameter("level", "Level", -24, 6, -10, "dB")
}} {}

void Fuzz::prepare(double sampleRate, int) {
    rate = std::max(sampleRate, 1000.0);
    for (auto& smoother : smoothers) smoother.prepare(rate, 20);
    for (auto& state : shapeState) state.dc.prepare(rate * 4);
    reset();
}

void Fuzz::reset() noexcept {
    for (std::size_t j = 0; j < controls.size(); ++j) smoothers[j].reset(controls[j]->get());
    for (auto& state : shapeState) state.dc.reset();
    previousInput.fill(0); highpass.fill(0); tone1.fill(0); tone2.fill(0); oversampler.reset();
}

float Fuzz::shape(float input, void* user) noexcept {
    auto& state = *static_cast<ShapeState*>(user);
    const float first = std::tanh(input * state.drive);
    const float second = std::tanh(first * state.sustain + 0.2f) - std::tanh(0.2f);
    return state.dc.process(second);
}

void Fuzz::process(ProcessContext& context) noexcept {
    const float pole = std::exp(-2 * pedal::pi * 25 / float(rate));
    for (int i = 0; i < context.audio.frames; ++i) {
        std::array<float, 4> values;
        for (std::size_t j = 0; j < values.size(); ++j)
            values[j] = smoothers[j].next(controls[j]->get());
        const float drive = pedal::gain(values[0]), sustain = 2 + 10 * values[1];
        const float cutoff = std::min(500.0f * std::pow(16.0f, values[2]), float(rate * 0.4));
        const float toneCoefficient = 1 - std::exp(-2 * pedal::pi * cutoff / float(rate));
        const float level = pedal::gain(values[3]);
        for (int c = 0; c < std::min(context.audio.channelCount, 2); ++c) {
            float& sample = context.audio.channel(c)[i];
            const float filtered = pole * (highpass[c] + sample - previousInput[c]);
            previousInput[c] = sample; highpass[c] = filtered;
            shapeState[c].drive = drive; shapeState[c].sustain = sustain;
            const float clipped = oversampler.process(filtered, c, &Fuzz::shape, &shapeState[c]);
            tone1[c] += toneCoefficient * (clipped - tone1[c]);
            tone2[c] += toneCoefficient * (tone1[c] - tone2[c]);
            sample = tone2[c] * level;
        }
    }
}
