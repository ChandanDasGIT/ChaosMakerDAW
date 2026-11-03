#include "crunch.h"

Crunch::Crunch() : controls{{
    &addParameter("drive", "Drive", 0, 30, 12, "dB"),
    &addParameter("tone", "Tone", 0, 1, 0.6f),
    &addParameter("body", "Body", -6, 6, 1, "dB"),
    &addParameter("level", "Level", -24, 6, -6, "dB")
}} {}

void Crunch::prepare(double sampleRate, int) {
    rate = std::max(sampleRate, 1000.0);
    for (auto& smoother : smoothers) smoother.prepare(rate, 20);
    for (auto& state : shapeState) state.dc.prepare(rate * 4);
    reset();
}

void Crunch::reset() noexcept {
    for (std::size_t i = 0; i < controls.size(); ++i) smoothers[i].reset(controls[i]->get());
    for (auto& state : shapeState) state.dc.reset();
    previousInput.fill(0); highpass.fill(0); body.fill(0); tone.fill(0);
    oversampler.reset();
}

float Crunch::shape(float input, void* user) noexcept {
    auto& state = *static_cast<ShapeState*>(user);
    const float driven = input * state.drive;
    const float asymmetric = driven * (driven < 0 ? 1.25f : 1.0f);
    return state.dc.process((2.0f / pedal::pi) * std::atan(asymmetric));
}

void Crunch::process(ProcessContext& context) noexcept {
    const float highpassPole = std::exp(-2 * pedal::pi * 70 / float(rate));
    const float bodyCoefficient = 1 - std::exp(-2 * pedal::pi * 220 / float(rate));
    for (int i = 0; i < context.audio.frames; ++i) {
        std::array<float, 4> values;
        for (std::size_t j = 0; j < values.size(); ++j)
            values[j] = smoothers[j].next(controls[j]->get());
        const float drive = pedal::gain(values[0]);
        const float bodyGain = pedal::gain(values[2]);
        const float level = pedal::gain(values[3]);
        const float cutoff = std::min(1200.0f * std::pow(8.0f, values[1]), float(rate * 0.4));
        const float toneCoefficient = 1 - std::exp(-2 * pedal::pi * cutoff / float(rate));
        for (int c = 0; c < std::min(context.audio.channelCount, 2); ++c) {
            float& sample = context.audio.channel(c)[i];
            const float filtered = highpassPole * (highpass[c] + sample - previousInput[c]);
            previousInput[c] = sample; highpass[c] = filtered;
            shapeState[c].drive = drive;
            const float clipped = oversampler.process(filtered, c, &Crunch::shape, &shapeState[c]);
            body[c] += bodyCoefficient * (clipped - body[c]);
            const float voiced = clipped + (bodyGain - 1) * body[c];
            tone[c] += toneCoefficient * (voiced - tone[c]);
            sample = tone[c] * level;
        }
    }
}
