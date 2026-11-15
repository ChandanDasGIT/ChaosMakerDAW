#include "distortion.h"

Distortion::Distortion() : controls{{
    &addParameter("drive", "Drive", 6, 48, 30, "dB"),
    &addParameter("tight", "Tight", 0, 1, 0.6f),
    &addParameter("bass", "Bass", -12, 12, 0, "dB"),
    &addParameter("mid", "Mids", -12, 12, 0, "dB"),
    &addParameter("midfreq", "Mid frequency", 300, 1600, 750, "Hz"),
    &addParameter("treble", "Treble", -12, 12, 0, "dB"),
    &addParameter("level", "Level", -24, 6, -10, "dB")
}} {}

void Distortion::prepare(double sampleRate, int) {
    rate = std::max(sampleRate, 1000.0);
    for (auto& smoother : smoothers) smoother.prepare(rate, 20);
    const double oversampled = rate * 4;
    for (auto& state : shapeState) {
        state.first.positiveKnee = 1.2f;  state.first.negativeKnee = 0.85f;
        state.first.conduction = 0.6f;    state.first.biasDepth = 0.4f;
        state.first.prepare(oversampled, 2.0f, 40.0f);
        state.second.positiveKnee = 1.0f; state.second.negativeKnee = 1.0f;
        state.second.conduction = 0.8f;   state.second.biasDepth = 0.2f;
        state.second.prepare(oversampled, 2.0f, 40.0f);
        state.trebleCoefficient = float(1.0 - std::exp(-2.0 * 3.14159265358979 * 7000.0 / oversampled));
    }
    bite.set({eqband::Shape::Bell, 1400.0, 4.0, 0.8, eqband::Slope::Db12}, rate);
    scoop.set({eqband::Shape::Bell, 320.0, -8.0, 0.6, eqband::Slope::Db12}, rate);
    rolloff.set({eqband::Shape::HighCut, 8000.0, 0.0, eqband::DefaultQ, eqband::Slope::Db12}, rate);
    reset();
}

void Distortion::reset() noexcept {
    std::array<float, 7> values{};
    for (std::size_t i = 0; i < controls.size(); ++i) { values[i] = controls[i]->get(); smoothers[i].reset(values[i]); }
    for (auto& state : shapeState) { state.first.reset(); state.second.reset(); state.treble = 0; }
    for (auto* f : {&tight, &bite, &scoop, &bass, &mid, &treble, &rolloff}) f->reset();
    oversampler.reset();
    frameCounter = 0;
    applied.fill(-1e9f);
    refresh(values);
}

// Rebuilds the filters whose controls moved (drive and level are applied per sample).
void Distortion::refresh(const std::array<float, 7>& v) noexcept {
    using eqband::Shape; using eqband::Slope;
    if (v[1] != applied[1]) tight.set({Shape::LowCut, 70.0 + 330.0 * v[1], 0.0, eqband::DefaultQ, Slope::Db12}, rate);
    if (v[2] != applied[2]) bass.set({Shape::LowShelf, 120.0, v[2], eqband::DefaultQ, Slope::Db12}, rate);
    if (v[3] != applied[3] || v[4] != applied[4]) mid.set({Shape::Bell, v[4], v[3], 0.7, Slope::Db12}, rate);
    if (v[5] != applied[5]) treble.set({Shape::HighShelf, 2800.0, v[5], eqband::DefaultQ, Slope::Db12}, rate);
    applied = v;
}

// Two stages: uneven, then a harder symmetric one, with treble loss between them.
float Distortion::shape(float input, void* user) noexcept {
    auto& state = *static_cast<ShapeState*>(user);
    const float first = state.first.process(input * state.stageGain);
    state.treble += state.trebleCoefficient * (first - state.treble);
    return state.second.process(state.treble * state.stageGain);
}

void Distortion::process(ProcessContext& context) noexcept {
    const int channels = std::min(context.audio.channelCount, 2);
    for (int i = 0; i < context.audio.frames; ++i) {
        std::array<float, 7> values;
        for (std::size_t j = 0; j < values.size(); ++j)
            values[j] = smoothers[j].next(controls[j]->get());
        if (frameCounter++ % RefreshFrames == 0) refresh(values);
        const float stageGain = std::sqrt(pedal::gain(values[0]));
        const float level = pedal::gain(values[6]);
        for (int c = 0; c < channels; ++c) {
            float& sample = context.audio.channel(c)[i];
            float y = bite.run(c, tight.run(c, sample));
            shapeState[c].stageGain = stageGain;
            y = oversampler.process(y, c, &Distortion::shape, &shapeState[c]);
            y = rolloff.run(c, treble.run(c, mid.run(c, bass.run(c, scoop.run(c, y)))));
            sample = y * level;
        }
    }
}
