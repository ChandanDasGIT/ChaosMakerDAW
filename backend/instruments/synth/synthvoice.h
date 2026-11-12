#pragma once
#include "instruments/synth/synthenvelope.h"
#include "instruments/synth/synthfilter.h"
#include "instruments/synth/synthoscillator.h"
#include "instruments/synth/synthsettings.h"

#include <cstdint>

// One sounding note: oscillators -> drive -> filter -> amp envelope.
namespace synth {

// What the whole synth works out once per block and every voice shares.
struct VoiceControls {
    const Settings* settings = nullptr;
    double sampleRate = 48000.0;
    double bendSemitones = 0.0;         // the pitch wheel, already scaled by the bend range
    // The LFO at full fade-in, already scaled by the depth for its target (0 elsewhere).
    double lfoSemitones = 0.0;
    double lfoOctaves = 0.0;            // the filter
    double lfoPulseWidth = 0.0;
    double lfoVolumeDip = 0.0;          // 0..1 taken off the level
};

class Voice {
public:
    // A new note on this voice. `retrigger`: start the envelopes again (from their present
    // level, no click). `glideSeconds` > 0 and a voice already sounding: slide from its pitch.
    void start(int key, int velocity, bool retrigger, double glideSeconds, double sampleRate) noexcept;
    void release() noexcept;
    void fadeOut(double sampleRate) noexcept;       // a few ms: the voice is being taken
    void stop() noexcept;                           // silent at once

    bool active() const noexcept { return !m_amp.idle(); }
    bool releasing() const noexcept { return m_amp.releasing(); }
    int key() const noexcept { return m_key; }
    float level() const noexcept { return m_amp.level(); }
    double pitch() const noexcept { return m_pitch; }   // in MIDI keys, glide included

    // Adds `frames` samples (mono) to `out`.
    void render(float* out, int frames, const VoiceControls& controls) noexcept;

private:
    float noise() noexcept;
    void updateFilter(const VoiceControls& c, float filterEnv) noexcept;

    static constexpr int FilterStep = 8;            // samples between filter updates

    int m_key = -1;
    double m_velocity = 1.0;                        // 0..1
    double m_pitch = 60.0, m_targetPitch = 60.0;
    double m_glideCoef = 0.0;                       // per sample; 0 = jump
    double m_lfoFade = 1.0, m_lfoFadeStep = 1.0;    // 0..1, after the LFO delay
    WaveOscillator m_osc[OscillatorCount];
    WaveOscillator m_sub;
    SvfFilter m_filter;
    AdsrEnvelope m_amp, m_filterEnv;
    std::uint32_t m_seed = 0x9E3779B9u;
};

} // namespace synth
