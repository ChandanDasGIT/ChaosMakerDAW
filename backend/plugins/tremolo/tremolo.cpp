#include "tremolo.h"
#include "dsp/tempodivision.h"

Tremolo::Tremolo() : controls{{
    &addParameter("rate", "Rate", 0.1f, 15.0f, 4.0f, "Hz"),
    &addParameter("depth", "Depth", 0.0f, 1.0f, 0.5f, ""),
    &addParameter("sync", "BPM sync", 0.0f, 1.0f, 0.0f),
    &addParameter("division", "Beat division", 0.0f, 6.0f, 2.0f),
    &addParameter("sustain", "Sustain only", 0, 1, 0),
    &addParameter("hold", "Attack delay", 20, 1000, 180, "ms"),
    &addParameter("fade", "Fade in", 20, 1500, 250, "ms"),
    &addParameter("threshold", "Note threshold", -80, -20, -55, "dB")
}} {}

void Tremolo::prepare(double sampleRate, int) {
    rate = std::max(sampleRate, 1000.0);
    for (auto& smoother : smoothers) smoother.prepare(rate, 15.0f);
    pulseRate.prepare(rate, 15.0f);
    fastAttack = 1 - std::exp(-1.0 / (rate * 0.002));
    fastRelease = 1 - std::exp(-1.0 / (rate * 0.025));
    slowCoefficient = 1 - std::exp(-1.0 / (rate * 0.080));
    blendCoefficient = 1 - std::exp(-1.0 / (rate * 0.005));
    reset();
}

void Tremolo::reset() noexcept {
    for (std::size_t j = 0; j < smoothers.size(); ++j) smoothers[j].reset(controls[j]->get());
    initialiseRate = true;
    phase = 0;
    fastEnvelope = slowEnvelope = sustainMix = 0;
    noteAge = 0; noteActive = attackLatched = false;
}

void Tremolo::process(ProcessContext& context) noexcept {
    // BPM describes quarter notes, independent of the time-signature denominator.
    const bool synced = controls[2]->get() >= 0.5f;
    const int division = std::clamp(int(std::round(controls[3]->get())), 0, 6);
    const float targetHz = float(std::clamp(synced ? tempo::noteFrequency(context.bpm, division)
                                                        : double(controls[0]->get()), 0.00001, rate * 0.45));
    if (initialiseRate) { pulseRate.reset(targetHz); initialiseRate = false; }
    const bool sustainOnly = controls[4]->get() >= 0.5f;
    const double holdSamples = controls[5]->get() * rate / 1000;
    const double fadeSamples = controls[6]->get() * rate / 1000;
    const float threshold = pedal::gain(controls[7]->get());
    const int channels = std::min(context.audio.channelCount, 2);
    for (int i = 0; i < context.audio.frames; ++i) {
        const float hz = pulseRate.next(targetHz);
        const float depth = smoothers[1].next(controls[1]->get());
        float peak = 0;
        for (int c = 0; c < channels; ++c) peak = std::max(peak, std::abs(context.audio.channel(c)[i]));
        fastEnvelope += (peak > fastEnvelope ? fastAttack : fastRelease) * (peak - fastEnvelope);
        slowEnvelope += slowCoefficient * (fastEnvelope - slowEnvelope);
        if (fastEnvelope < threshold * 0.5f) {
            noteActive = false; attackLatched = false; noteAge = 0;
        } else if (fastEnvelope >= threshold) {
            const bool attack = fastEnvelope > std::max(threshold, slowEnvelope * 1.7f);
            if (!noteActive || (attack && !attackLatched && noteAge > std::uint64_t(rate * 0.05))) {
                noteAge = 0; noteActive = true; attackLatched = true;
            }
            if (fastEnvelope < slowEnvelope * 1.15f) attackLatched = false;
        }
        const float target = !sustainOnly ? 1.0f : !noteActive ? 0.0f
            : float(std::clamp((double(noteAge) - holdSamples) / fadeSamples, 0.0, 1.0));
        // Keep the original mode sample-identical. Smooth transitions in sustain mode.
        if (!sustainOnly) sustainMix = 1;
        else sustainMix += blendCoefficient * (target - sustainMix);
        if (noteActive && noteAge < std::uint64_t(rate * 3600)) ++noteAge;
        const float amplitude = 1.0f - depth * sustainMix * (0.5f + 0.5f * std::sin(float(phase)));
        for (int c = 0; c < channels; ++c) context.audio.channel(c)[i] *= amplitude;
        // Preserve phase on tempo, division and mode changes; glide the rate.
        phase += 2.0 * pedal::pi * hz / rate;
        if (phase >= 2.0 * pedal::pi) phase -= 2.0 * pedal::pi;
    }
}
