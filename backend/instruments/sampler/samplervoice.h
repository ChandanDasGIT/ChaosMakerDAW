#pragma once
#include "instruments/sampler/samplezone.h"

#include <cstdint>

// One playing note of the sampler: reads one zone's sample at the note's pitch, shapes it with
// the zone's envelope, and adds it to a stereo output. Everything here runs on the audio thread:
// no allocation, and the zone it plays must outlive the note.
//
// Conventions follow SFZ:
//   pitch      the sample plays at its own pitch on rootKey; tuneCents shifts it further
//   velocity   gain is the zone's velocity curve, or (velocity / 127)^2 (amp_veltrack 100 %)
//   offset     the sample starts at the zone's offset frame; delaySeconds of silence come first
//              (the envelope starts when the sample does)
//   envelope   linear attack, `hold` seconds at full level, then exponential decay to
//              sustain; release falls exponentially from wherever the level is, reaching
//              -60 dB after the release time (which may change while it runs)
//   filter     a zone with cutoffHz > 0 goes through a 2-pole low-pass (state-variable,
//              resonance in dB over Butterworth), each channel on its own
//   pan        -1 (left) .. +1 (right); a mono sample is panned with equal power, unity in
//              the centre; a stereo sample is balanced (the far channel is turned down)
//   loops      loopEnd is the last frame of the loop (inclusive, like loop_end); a looping
//              zone keeps looping through its release
class SamplerVoice {
public:
    enum class Stage { Off, Attack, Hold, Decay, Sustain, Release };

    // Starts the note at the zone's offset, after its delay. outputRate is the engine's rate.
    // A silent zone has no sample: it must not be started.
    void start(const SampleZone& zone, int key, int velocity, double outputRate) noexcept;

    // Starts the release from the current level (a held note was let go), lasting the zone's
    // release, or `seconds` when given (a pedal-dependent release).
    void release() noexcept;
    void release(float seconds) noexcept;
    // A release under way now takes `seconds` from here (the pedal moved). Not a fade.
    void setReleaseSeconds(float seconds) noexcept;

    // Fades out without a click: a choked hi-hat, a stolen voice (a few milliseconds), or a
    // piano string struck again (the zone's off time).
    void fadeOut() noexcept;
    void fadeOut(float seconds) noexcept;

    // Kept sounding by the sustain pedal after its key was let go (the sampler's bookkeeping).
    bool pedalHeld() const noexcept { return m_pedalHeld; }
    void setPedalHeld(bool held) noexcept { m_pedalHeld = held; }

    // Silences at once.
    void stop() noexcept;

    // Adds up to `frames` frames to left and right. Stops by itself at the end of a sample
    // that does not loop, or when the release has faded out.
    void render(float* left, float* right, int frames) noexcept;

    bool active() const noexcept { return m_stage != Stage::Off; }
    bool releasing() const noexcept { return m_stage == Stage::Release; }
    bool fading() const noexcept { return m_stage == Stage::Release && m_fading; }
    Stage stage() const noexcept { return m_stage; }
    float level() const noexcept { return m_level; }      // envelope level, 0..1
    int key() const noexcept { return m_key; }
    const SampleZone* zone() const noexcept { return m_zone; }
    double position() const noexcept { return m_position; }   // in sample frames
    double increment() const noexcept { return m_increment; }

private:
    float sampleAt(int channel, std::int64_t index) const noexcept;
    float interpolate(int channel, std::int64_t index, float fraction) const noexcept;
    float nextEnvelope() noexcept;

    const SampleZone* m_zone = nullptr;
    Stage m_stage = Stage::Off;
    int m_key = -1;
    double m_position = 0.0;
    std::int64_t m_delayFrames = 0;                        // output frames of silence left before the sample
    double m_increment = 1.0;
    bool m_looping = false;
    std::int64_t m_loopStart = 0, m_loopEnd = 0;           // checked against the sample length

    struct Filter {                                        // TPT state-variable low-pass
        float g = 0.0f, k = 1.4142f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        float ic1[2] = {0.0f, 0.0f}, ic2[2] = {0.0f, 0.0f};
        float process(int channel, float x) noexcept;
    };

    double m_rate = 48000.0;
    float m_level = 0.0f;
    float m_attackStep = 1.0f;                             // level added per frame
    std::int64_t m_holdFrames = 0;                         // left in the hold stage
    bool m_filtered = false;
    Filter m_filter;
    bool m_pedalHeld = false;
    float m_decayCoef = 0.0f, m_releaseCoef = 0.0f;        // per-frame multipliers toward the target
    float m_fadeCoef = 0.0f;
    bool m_fading = false;
    float m_sustain = 1.0f;
    float m_gainLeft = 0.0f, m_gainRight = 0.0f;           // velocity, volume and pan together
};
