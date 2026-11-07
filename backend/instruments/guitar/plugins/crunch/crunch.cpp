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
    for (auto& state : shapeState) {
        state.stage.positiveKnee = 1.35f;   // uneven, like a single tube stage
        state.stage.negativeKnee = 0.8f;
        state.stage.conduction = 0.5f;
        state.stage.biasDepth = 0.6f;
        state.stage.prepare(rate * 4, 2.0f, 50.0f);
    }
    lowCut.set({eqband::Shape::LowCut, 80.0, 0.0, eqband::DefaultQ, eqband::Slope::Db6}, rate);
    presence.set({eqband::Shape::Bell, 1100.0, 3.0, 0.7, eqband::Slope::Db12}, rate);
    scoop.set({eqband::Shape::Bell, 350.0, -5.0, 0.6, eqband::Slope::Db12}, rate);
    rolloff.set({eqband::Shape::HighCut, 9000.0, 0.0, eqband::DefaultQ, eqband::Slope::Db12}, rate);
    reset();
}

void Crunch::reset() noexcept {
    for (std::size_t i = 0; i < controls.size(); ++i) smoothers[i].reset(controls[i]->get());
    for (auto& state : shapeState) state.stage.reset();
    for (auto* f : {&lowCut, &presence, &scoop, &bodyShelf, &toneShelf, &rolloff}) f->reset();
    oversampler.reset();
    frameCounter = 0;
    appliedTone = -1; appliedBody = -100;
    refresh(controls[1]->get(), controls[2]->get());
}

void Crunch::refresh(float tone, float body) noexcept {
    if (tone != appliedTone) {
        appliedTone = tone;
        toneShelf.set({eqband::Shape::HighShelf, 2200.0, -12.0 + 18.0 * tone, eqband::DefaultQ, eqband::Slope::Db12}, rate);
    }
    if (body != appliedBody) {
        appliedBody = body;
        bodyShelf.set({eqband::Shape::LowShelf, 200.0, body, eqband::DefaultQ, eqband::Slope::Db12}, rate);
    }
}

float Crunch::shape(float input, void* user) noexcept {
    auto& state = *static_cast<ShapeState*>(user);
    return state.stage.process(input * state.drive);
}

void Crunch::process(ProcessContext& context) noexcept {
    for (int i = 0; i < context.audio.frames; ++i) {
        std::array<float, 4> values;
        for (std::size_t j = 0; j < values.size(); ++j)
            values[j] = smoothers[j].next(controls[j]->get());
        if (frameCounter++ % RefreshFrames == 0) refresh(values[1], values[2]);
        const float drive = pedal::gain(values[0]);
        const float level = pedal::gain(values[3]);
        for (int c = 0; c < std::min(context.audio.channelCount, 2); ++c) {
            float& sample = context.audio.channel(c)[i];
            float y = presence.run(c, lowCut.run(c, sample));
            shapeState[c].drive = drive;
            y = oversampler.process(y, c, &Crunch::shape, &shapeState[c]);
            y = rolloff.run(c, toneShelf.run(c, bodyShelf.run(c, scoop.run(c, y))));
            sample = y * level;
        }
    }
}
