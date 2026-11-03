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
    for (auto& state : shapeState) state.dc.prepare(rate * 4);

    // Constant for a given sample rate: compute once here, not per block.
    const float r = float(rate);
    lowCoefficient = 1 - std::exp(-2 * pedal::pi * 200 / r);
    highCoefficient = 1 - std::exp(-2 * pedal::pi * 2800 / r);
    outputCoefficient = 1 - std::exp(-2 * pedal::pi * float(std::min(8500.0, rate * 0.4)) / r);
    reset();
}

void Distortion::reset() noexcept {
    for (std::size_t i = 0; i < controls.size(); ++i) smoothers[i].reset(controls[i]->get());
    for (auto& state : shapeState) state.dc.reset();
    previousInput.fill(0); highpass.fill(0); low.fill(0); high.fill(0);
    mid1.fill(0); mid2.fill(0); outputLowpass.fill(0); oversampler.reset();
    controlCounter = 0; // forces a coefficient refresh on the first sample
}

// Asymmetric soft clipper: tanh(x + bias) - tanh(bias).
// Smooth everywhere (no corner), so far less aliasing than a hard clamp;
// the bias adds even harmonics. The DC blocker removes the offset it creates.
float Distortion::shape(float input, void* user) noexcept {
    auto& state = *static_cast<ShapeState*>(user);
    static const float biasOffset = std::tanh(kBias);
    const float clipped = std::tanh(input * state.drive * 0.6f + kBias) - biasOffset;
    return state.dc.process(clipped);
}

void Distortion::updateCoefficients(const std::array<float, 7>& values) noexcept {
    drive = pedal::gain(values[0]);
    highpassPole = std::exp(-2 * pedal::pi * (70 + values[1] * 180) / float(rate));
    bass = pedal::gain(values[2]);
    mids = pedal::gain(values[3]);
    treble = pedal::gain(values[5]);
    level = pedal::gain(values[6]);
    // TPT state-variable band-pass forms a peaking mid EQ: unity when Mids=0 dB.
    constexpr float k = 1.4f;
    const float frequency = std::min(values[4], float(rate * 0.4));
    g = std::tan(pedal::pi * frequency / float(rate));
    a = 1 / (1 + g * (g + k));
}

void Distortion::process(ProcessContext& context) noexcept {
    constexpr float k = 1.4f;
    const int channels = std::min(context.audio.channelCount, 2);
    for (int i = 0; i < context.audio.frames; ++i) {
        // Smoothers advance every sample so their ramps stay accurate.
        std::array<float, 7> values;
        for (std::size_t j = 0; j < values.size(); ++j)
            values[j] = smoothers[j].next(controls[j]->get());
        // The expensive derived math runs only every kControlInterval samples.
        if (controlCounter == 0) updateCoefficients(values);
        if (++controlCounter >= kControlInterval) controlCounter = 0;

        for (int c = 0; c < channels; ++c) {
            float& sample = context.audio.channel(c)[i];
            const float filtered = highpassPole * (highpass[c] + sample - previousInput[c]);
            previousInput[c] = sample; highpass[c] = filtered;
            shapeState[c].drive = drive;
            const float clipped = oversampler.process(filtered, c, &Distortion::shape, &shapeState[c]);
            low[c] += lowCoefficient * (clipped - low[c]);
            high[c] += highCoefficient * (clipped - high[c]);
            const float voiced = clipped + (bass - 1) * low[c] + (treble - 1) * (clipped - high[c]);
            const float v1 = a * (mid1[c] + g * (voiced - mid2[c]));
            const float v2 = mid2[c] + g * v1;
            mid1[c] = 2 * v1 - mid1[c]; mid2[c] = 2 * v2 - mid2[c];
            const float equalized = voiced + (mids - 1) * k * v1;
            outputLowpass[c] += outputCoefficient * (equalized - outputLowpass[c]);
            sample = outputLowpass[c] * level;
        }
    }
}
