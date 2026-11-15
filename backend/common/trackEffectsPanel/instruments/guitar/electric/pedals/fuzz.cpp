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
    for (auto& state : shapeState) {
        state.first.positiveKnee = 1.6f;  state.first.negativeKnee = 0.6f;   // starved transistor
        state.first.conduction = 0.3f;    state.first.biasDepth = 0.9f;
        state.first.prepare(rate * 4, 1.0f, 80.0f);
        state.second.positiveKnee = 1.0f; state.second.negativeKnee = 1.0f;
        state.second.conduction = 0.8f;   state.second.biasDepth = 0.2f;
        state.second.prepare(rate * 4, 2.0f, 40.0f);
    }
    lowCut.set({eqband::Shape::LowCut, 60.0, 0.0, eqband::DefaultQ, eqband::Slope::Db6}, rate);
    dark.set({eqband::Shape::HighCut, 600.0, 0.0, eqband::DefaultQ, eqband::Slope::Db6}, rate);
    bright.set({eqband::Shape::LowCut, 1000.0, 0.0, eqband::DefaultQ, eqband::Slope::Db6}, rate);
    rolloff.set({eqband::Shape::HighCut, 9000.0, 0.0, eqband::DefaultQ, eqband::Slope::Db12}, rate);
    reset();
}

void Fuzz::reset() noexcept {
    for (std::size_t j = 0; j < controls.size(); ++j) smoothers[j].reset(controls[j]->get());
    for (auto& state : shapeState) { state.first.reset(); state.second.reset(); }
    for (auto* f : {&lowCut, &dark, &bright, &rolloff}) f->reset();
    oversampler.reset();
}

float Fuzz::shape(float input, void* user) noexcept {
    auto& state = *static_cast<ShapeState*>(user);
    const float first = state.first.process(input * state.drive);
    return state.second.process(first * state.sustain);
}

void Fuzz::process(ProcessContext& context) noexcept {
    for (int i = 0; i < context.audio.frames; ++i) {
        std::array<float, 4> values;
        for (std::size_t j = 0; j < values.size(); ++j)
            values[j] = smoothers[j].next(controls[j]->get());
        const float drive = pedal::gain(values[0]), sustain = 2 + 10 * values[1];
        const float tone = values[2];
        const float level = pedal::gain(values[3]);
        for (int c = 0; c < std::min(context.audio.channelCount, 2); ++c) {
            float& sample = context.audio.channel(c)[i];
            shapeState[c].drive = drive; shapeState[c].sustain = sustain;
            const float clipped = oversampler.process(lowCut.run(c, sample), c, &Fuzz::shape, &shapeState[c]);
            // Both paths run every sample so moving Tone never jumps.
            const float y = (1 - tone) * dark.run(c, clipped) + tone * 1.26f * bright.run(c, clipped);
            sample = rolloff.run(c, y) * level;
        }
    }
}
