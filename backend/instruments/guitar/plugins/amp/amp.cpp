#include "amp.h"

#include <algorithm>
#include <cmath>

namespace {

// Per-channel voicing. Gains in dB; drive maps to preamp gain as drive * driveScale + driveOffset.
struct Voicing {
    int stages;
    float driveScale, driveOffset;
    float tightHz; eqband::Slope tightSlope;      // 0 Hz: no low cut before the preamp
    eqband::Shape pushShape; float pushHz, pushDb, pushQ;
    float brightDb;                               // treble boost (above 1.5 kHz) into the preamp
    float couplingHz;                             // low cut between preamp stages
    float trebleLossHz;                           // treble loss between preamp stages
    float scoopHz, scoopDb, scoopQ;               // the tone stack's own mid scoop at noon
    float powerDrive, sag;
    float trimDb;                                 // keeps the channels at similar loudness
};

constexpr Voicing voicings[3] = {
    // Clean: a light bright boost, lots of headroom.
    {2, 0.5f, -8.0f, 0.0f, eqband::Slope::Db6, eqband::Shape::HighShelf, 2500.0f, 4.0f, 0.7f,
     0.0f, 30.0f, 14000.0f, 500.0f, -4.0f, 0.6f, 0.7f, 0.12f, 0.0f},
    // Crunch: a little low cut and mid push.
    {2, 1.0f, -6.0f, 90.0f, eqband::Slope::Db6, eqband::Shape::Bell, 900.0f, 3.0f, 0.7f,
     4.0f, 60.0f, 12000.0f, 350.0f, -9.0f, 0.5f, 1.0f, 0.22f, 0.0f},
    // High Gain: tight low cut and a mid push before three stages (the boosted-amp recipe).
    {3, 1.0f, 4.0f, 200.0f, eqband::Slope::Db12, eqband::Shape::Bell, 750.0f, 7.0f, 0.6f,
     6.0f, 110.0f, 12000.0f, 300.0f, -14.0f, 0.5f, 1.0f, 0.28f, 0.0f},
};

// A triode-like curve: slope 1 at rest, the positive side (grid conduction) saturates sooner
// than the negative side (cutoff), which adds even harmonics.
float triode(float x) noexcept
{
    return x >= 0.0f ? std::tanh(1.4f * x) / 1.4f : std::tanh(0.75f * x) / 0.75f;
}

float onePole(double rate, double hz) noexcept
{
    return float(1.0 - std::exp(-2.0 * 3.14159265358979 * hz / rate));
}

int channelOf(float value) noexcept
{
    return std::clamp(int(std::lround(value)), 0, 2);
}

} // namespace

Amp::Amp() : controls{{
    &addParameter("drive","Drive",0,40,14,"dB"),
    &addParameter("bass","Bass",-12,12,0,"dB"),
    &addParameter("mid","Mid",-12,12,0,"dB"),
    &addParameter("treble","Treble",-12,12,0,"dB"),
    &addParameter("presence","Presence",0,1,0.3f),
    &addParameter("master","Master",-36,6,-12,"dB"),
    &addParameter("channel","Channel",0,2,1,"channel"),
    &addParameter("resonance","Resonance",0,1,0.3f)
}} {}

float Amp::Filter::run(int channel, float x) noexcept
{
    if (!active) return x;
    double y = x;
    auto& s = state[std::size_t(channel)];
    for (int k = 0; k < design.sectionCount; ++k)
        y = biquad::process(design.sections[std::size_t(k)], s[std::size_t(k)], y);
    return float(y);
}

void Amp::Filter::set(const eqband::Settings& settings, double rate) noexcept
{
    design = eqband::design(settings, rate);
    active = true;
}

void Amp::prepare(double sampleRate,int) {
    rate=std::max(sampleRate,1000.0);
    for(auto& smoother:smoothers) smoother.prepare(rate,20);
    reset();
}

void Amp::reset() noexcept {
    std::array<float, ParameterCount> values{};
    for(std::size_t i=0;i<controls.size();++i) { values[i]=controls[i]->get(); smoothers[i].reset(values[i]); }
    for(auto* f:{&inputCut,&tight,&push,&bright,&scoop,&bass,&mid,&treble,&presence,&resonance,&outputCut})
        for(auto& channel:f->state) channel={};
    for(auto& p:preamps) { p.bias={}; p.couplingIn={}; p.couplingOut={}; p.treble={}; }
    for(auto& p:powers) p.envelope=0;
    preampOversampler.reset(); powerOversampler.reset();
    frameCounter=0;
    applied.fill(-1e9f);
    appliedChannel = -1;
    refresh(values);
}

// Rebuilds the filters and stage settings from the (smoothed) control values.
void Amp::refresh(const std::array<float, ParameterCount>& v) noexcept
{
    if (v == applied) return;
    const bool voicingChanged = channelOf(v[6]) != appliedChannel;
    applied = v;
    appliedChannel = channelOf(v[6]);
    const auto& voice = voicings[channelOf(v[6])];
    using eqband::Shape; using eqband::Slope;
    if (voicingChanged) {
        inputCut.set({Shape::LowCut, 30.0, 0.0, eqband::DefaultQ, Slope::Db12}, rate);
        tight.active = voice.tightHz > 0;
        if (tight.active) tight.set({Shape::LowCut, voice.tightHz, 0.0, eqband::DefaultQ, voice.tightSlope}, rate);
        push.set({voice.pushShape, voice.pushHz, voice.pushDb, voice.pushQ, Slope::Db12}, rate);
        bright.active = voice.brightDb != 0.0f;
        if (bright.active) bright.set({Shape::HighShelf, 1500.0, voice.brightDb, eqband::DefaultQ, Slope::Db12}, rate);
        scoop.set({Shape::Bell, voice.scoopHz, voice.scoopDb, voice.scoopQ, Slope::Db12}, rate);
        outputCut.set({Shape::HighCut, 14000.0, 0.0, eqband::DefaultQ, Slope::Db12}, rate);
        const double oversampled = rate * 4;
        for (auto& p : preamps) {
            p.stages = voice.stages;
            p.couplingCoefficient = float(std::exp(-2.0 * 3.14159265358979 * voice.couplingHz / oversampled));
            p.trebleCoefficient = onePole(oversampled, voice.trebleLossHz);
            p.biasAttack = onePole(oversampled, 1000.0 / (2 * 3.14159265358979 * 2.0));     // ~2 ms
            p.biasRelease = onePole(oversampled, 1000.0 / (2 * 3.14159265358979 * 60.0));   // ~60 ms
        }
        for (auto& p : powers) {
            p.drive = voice.powerDrive;
            p.sag = voice.sag;
            p.attack = onePole(oversampled, 1000.0 / (2 * 3.14159265358979 * 5.0));
            p.release = onePole(oversampled, 1000.0 / (2 * 3.14159265358979 * 150.0));
        }
    }
    const float preGainDb = v[0] * voice.driveScale + voice.driveOffset;
    const float stageGain = std::pow(pedal::gain(preGainDb), 1.0f / float(voice.stages));
    for (auto& p : preamps) p.stageGain = stageGain;
    bass.set({Shape::LowShelf, 110.0, v[1], eqband::DefaultQ, Slope::Db12}, rate);
    mid.set({Shape::Bell, 650.0, v[2], 0.7, Slope::Db12}, rate);
    treble.set({Shape::HighShelf, 2800.0, v[3], eqband::DefaultQ, Slope::Db12}, rate);
    presence.set({Shape::HighShelf, 3800.0, v[4] * 9.0, eqband::DefaultQ, Slope::Db12}, rate);
    resonance.set({Shape::Bell, 90.0, v[7] * 9.0, 1.0, Slope::Db12}, rate);
    outputTrim = pedal::gain(voice.trimDb + v[5]);
}

float Amp::preamp(float x, void* user) noexcept {
    auto& p = *static_cast<Preamp*>(user);
    for (int stage = 0; stage < p.stages; ++stage) {
        const auto s = std::size_t(stage);
        const float driven = x * p.stageGain;
        float y = triode(driven - 0.5f * p.bias[s]);
        // Grid conduction: strong positive swings charge the coupling cap and shift the bias.
        const float charge = std::max(0.0f, driven - 0.6f);
        p.bias[s] += (charge > p.bias[s] ? p.biasAttack : p.biasRelease) * (charge - p.bias[s]);
        // Coupling cap (low cut) and treble loss into the next stage.
        const float coupled = p.couplingCoefficient * (p.couplingOut[s] + y - p.couplingIn[s]);
        p.couplingIn[s] = y;
        p.couplingOut[s] = coupled;
        p.treble[s] += p.trebleCoefficient * (coupled - p.treble[s]);
        x = p.treble[s];
    }
    return x;
}

float Amp::power(float x, void* user) noexcept {
    auto& p = *static_cast<Power*>(user);
    const float level = std::abs(x);
    p.envelope += (level > p.envelope ? p.attack : p.release) * (level - p.envelope);
    const float drive = p.drive / (1.0f + p.sag * p.envelope);
    return std::tanh(drive * x);
}

void Amp::process(ProcessContext& ctx) noexcept {
    const int channels = std::min(ctx.audio.channelCount, 2);
    std::array<float, ParameterCount> values{};
    for (int i = 0; i < ctx.audio.frames; ++i) {
        for (std::size_t j = 0; j < values.size(); ++j) values[j] = smoothers[j].next(controls[j]->get());
        values[6] = controls[6]->get();   // the channel switches, it does not glide
        if (frameCounter % RefreshFrames == 0) refresh(values);
        ++frameCounter;
        for (int c = 0; c < channels; ++c) {
            float& x = ctx.audio.channel(c)[i];
            float y = inputCut.run(c, x);
            y = tight.run(c, y);
            y = push.run(c, y);
            y = bright.run(c, y);
            y = preampOversampler.process(y, c, &Amp::preamp, &preamps[std::size_t(c)]);
            y = scoop.run(c, y);
            y = bass.run(c, y);
            y = mid.run(c, y);
            y = treble.run(c, y);
            y = presence.run(c, y);
            y = resonance.run(c, y);
            y = powerOversampler.process(y, c, &Amp::power, &powers[std::size_t(c)]);
            y = outputCut.run(c, y);
            x = y * outputTrim;
        }
    }
}
