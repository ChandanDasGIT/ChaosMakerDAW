#include "parametriceq.h"

ParametricEq::ParametricEq() : controls{{
    &addParameter("lowfreq", "Low frequency", 80, 500, 150, "Hz"),
    &addParameter("lowgain", "Low gain", -12, 12, 0, "dB"),
    &addParameter("lowq", "Low Q", 0.3f, 4, 0.7f),
    &addParameter("midfreq", "Mid frequency", 200, 2500, 900, "Hz"),
    &addParameter("midgain", "Mid gain", -12, 12, 0, "dB"),
    &addParameter("midq", "Mid Q", 0.3f, 4, 1),
    &addParameter("highfreq", "High frequency", 1500, 10000, 4000, "Hz"),
    &addParameter("highgain", "High gain", -12, 12, 0, "dB"),
    &addParameter("highq", "High Q", 0.3f, 4, 0.7f),
    &addParameter("level", "Level", -12, 12, 0, "dB")
}} {}

void ParametricEq::prepare(double sampleRate, int) {
    rate = std::max(sampleRate, 1000.0);
    for (auto& smoother : smoothers) smoother.prepare(rate, 20);
    reset();
}

void ParametricEq::reset() noexcept {
    state = {};
    for (std::size_t j = 0; j < controls.size(); ++j) smoothers[j].reset(controls[j]->get());
}

void ParametricEq::process(ProcessContext& context) noexcept {
    for (int i = 0; i < context.audio.frames; ++i) {
        std::array<float, 10> values;
        for (std::size_t j = 0; j < values.size(); ++j)
            values[j] = smoothers[j].next(controls[j]->get());
        for (int band = 0; band < 3; ++band) {
            const int offset = band * 3;
            const float frequency = std::min(values[offset], float(rate * 0.45));
            const float g = std::tan(pedal::pi * frequency / float(rate));
            const float k = 1 / values[offset + 2];
            const float a = 1 / (1 + g * (g + k));
            const float gain = pedal::gain(values[offset + 1]);
            for (int c = 0; c < std::min(context.audio.channelCount, 2); ++c) {
                auto& s = state[band][c];
                float& sample = context.audio.channel(c)[i];
                const float v1 = a * (s.ic1 + g * (sample - s.ic2));
                const float v2 = s.ic2 + g * v1;
                s.ic1 = 2 * v1 - s.ic1; s.ic2 = 2 * v2 - s.ic2;
                sample += (gain - 1) * k * v1;
            }
        }
        const float level = pedal::gain(values[9]);
        for (int c = 0; c < std::min(context.audio.channelCount, 2); ++c)
            context.audio.channel(c)[i] *= level;
    }
}
