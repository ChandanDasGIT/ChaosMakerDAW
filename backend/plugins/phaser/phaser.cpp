#include "phaser.h"
#include "dsp/tempodivision.h"

Phaser::Phaser() : controls{{
    &addParameter("rate", "Rate", 0.05f, 12, 0.4f, "Hz"),
    &addParameter("depth", "Depth", 0, 1, 0.7f),
    &addParameter("feedback", "Feedback", 0, 0.75f, 0.35f),
    &addParameter("mix", "Mix", 0, 1, 0.5f),
    &addParameter("level", "Level", -24, 6, 0, "dB"),
    &addParameter("sync", "BPM sync", 0, 1, 0),
    &addParameter("division", "Beat division", 0, 6, 1)
}} {}

void Phaser::prepare(double sampleRate, int) {
    rate = std::max(sampleRate, 1000.0);
    for (auto& smoother : smoothers) smoother.prepare(rate, 20);
    sweepRate.prepare(rate, 20);
    reset();
}

void Phaser::reset() noexcept {
    stageState = {}; feedback.fill(0); phase = 0; initialiseRate = true;
    for (std::size_t j = 0; j < smoothers.size(); ++j) smoothers[j].reset(controls[j]->get());
}

void Phaser::process(ProcessContext& context) noexcept {
    static constexpr std::array<float, 6> offsets{0.6f, 0.8f, 1, 1.3f, 1.7f, 2.2f};
    const bool synced = controls[5]->get() >= 0.5f;
    const int division = int(std::round(controls[6]->get()));
    const float targetHz = float(std::clamp(synced ? tempo::noteFrequency(context.bpm, division)
                                                : double(controls[0]->get()), 0.00001, rate * 0.45));
    if (initialiseRate) { sweepRate.reset(targetHz); initialiseRate = false; }
    for (int i = 0; i < context.audio.frames; ++i) {
        std::array<float, 5> values;
        for (std::size_t j = 0; j < values.size(); ++j)
            values[j] = smoothers[j].next(controls[j]->get());
        const float center = 600 * std::pow(2.0f, 2 * values[1] * float(std::sin(phase)));
        const float feedbackGain = values[2], mix = values[3], level = pedal::gain(values[4]);
        std::array<float, 6> coefficients;
        for (int stage = 0; stage < 6; ++stage) {
            const float frequency = std::min(center * offsets[stage], float(rate * 0.4));
            const float g = std::tan(pedal::pi * frequency / float(rate));
            coefficients[stage] = (1 - g) / (1 + g);
        }
        for (int c = 0; c < std::min(context.audio.channelCount, 2); ++c) {
            float& sample = context.audio.channel(c)[i];
            float wet = (sample + feedbackGain * feedback[c]) * (1 - feedbackGain);
            for (int stage = 0; stage < 6; ++stage) {
                const float output = stageState[c][stage] - coefficients[stage] * wet;
                stageState[c][stage] = wet + coefficients[stage] * output;
                wet = output;
            }
            feedback[c] = wet;
            sample = (sample * (1 - mix) + wet * mix) * level;
        }
        phase += 2 * double(pedal::pi) * sweepRate.next(targetHz) / rate;
        if (phase >= 2 * double(pedal::pi)) phase -= 2 * double(pedal::pi);
    }
}
