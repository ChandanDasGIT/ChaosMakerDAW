#include "noisegate.h"
NoiseGate::NoiseGate() : controls{{&addParameter("threshold", "Threshold", -80.0f, -10.0f, -48.0f, "dB"),
        &addParameter("attack", "Attack", 0.1f, 30.0f, 2.0f, "ms"),
        &addParameter("release", "Release", 10.0f, 500.0f, 100.0f, "ms")}} {}
void NoiseGate::prepare(double sampleRate, int) {
    rate = std::max(sampleRate, 1000.0);
    holdFrames = int(HoldSeconds * rate);
    for (auto& smoother : smoothers) smoother.prepare(rate, 15.0f);
    reset();
}
void NoiseGate::reset() noexcept {
    for (std::size_t j = 0; j < controls.size(); ++j) smoothers[j].reset(controls[j]->get());
    envelope = opening = 0; open = false; holdLeft = 0;
}
void NoiseGate::process(ProcessContext& context) noexcept {
    const int channels = std::min(context.audio.channelCount, 2);
    for (int i = 0; i < context.audio.frames; ++i) {
        std::array<float, 3> v;
        for (std::size_t j = 0; j < controls.size(); ++j) v[j] = smoothers[j].next(controls[j]->get());

        const float input = pedal::peak(context, i);
        envelope += pedal::coefficient(rate, input > envelope ? 1.0f : 30.0f) * (input - envelope);
        const float threshold = pedal::gain(v[0]);
        if (envelope >= threshold) { open = true; holdLeft = holdFrames; }
        else if (envelope >= threshold * 0.5f) holdLeft = holdFrames;   // 6 dB hysteresis: stay open
        else if (open && --holdLeft <= 0) open = false;                 // below: close after the hold
        opening += pedal::coefficient(rate, open ? v[1] : v[2]) * ((open ? 1.0f : 0.0f) - opening);
        for (int c = 0; c < channels; ++c) context.audio.channel(c)[i] *= opening;

    }
}
