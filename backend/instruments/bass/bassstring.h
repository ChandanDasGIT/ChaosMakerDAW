#pragma once
#include "instruments/bass/bassexciter.h"
#include "instruments/bass/basspickup.h"

#include <vector>

// One vibrating bass string: a Karplus-Strong / waveguide loop.
//
//   exciter -> + -> delay line (one period) -> loss filter -> stiffness all-pass -> back to +
//                         `-> pickups (combs read from the same line)
//
// The delay line holds one period of the wave; each trip round the loop loses a little, more
// in the highs than the lows. The loss is set by two decay times (T60, to -60 dB): one for the
// fundamental and one for the overtones around 2 kHz, so a low E rings as long in seconds as a
// high G even though it goes round fewer times. The loss filter is a one-pole low-pass; its
// gain and corner are worked out from the two times for each pitch. The read point is a
// fractional delay (cubic interpolation) shortened by the filters' own delay at the
// fundamental, so the string is in tune, and it can move smoothly for slides and bends.
// A slight all-pass makes the higher overtones a touch sharp, as on a stiff, wound string.
namespace bass {

// What the instrument works out once per block and every string shares.
struct StringControls {
    double sampleRate = 48000.0;
    double bendSemitones = 0.0;                    // the pitch wheel, scaled by the bend range
    double ringSeconds = 6.0;                      // T60 of the fundamental (Settings::sustain)
    double overtoneSeconds = 0.4;                  // T60 around 2 kHz (Settings::brightness)
    double muteAmount = 0.6;                       // for notes played muted
    double pickupBlend = 0.4;
};

class BassString {
public:
    static constexpr double LowestHz = 15.0;       // the delay line is long enough for this

    // Allocates the delay line. UI or worker thread, never while rendering.
    void prepare(double sampleRate);

    // A new note: `key` (a MIDI key, the open string's key + fret), struck as `kind`.
    // strength / brightness 0..1 from the velocity, strikeAt: the pluck position's fraction
    // of the string from the bridge.
    void start(int openKey, double key, NoteKind kind, double strength, double brightness, double strikeAt,
               const StringControls& controls) noexcept;
    // Slides to `key` over `seconds` (straight in semitones, as a finger moves along the frets).
    void slideTo(double key, double seconds, double sampleRate) noexcept;
    void release() noexcept;                       // the finger lifts: the string is damped
    void fadeOut(double sampleRate) noexcept;      // a few ms: another note takes the string
    void stop() noexcept;                          // silent at once

    bool active() const noexcept { return m_active; }
    bool releasing() const noexcept { return m_released || m_fadeStep > 0.0; }
    double pitch() const noexcept { return m_pitch; } // in MIDI keys, slide included, no bend
    double peak() const noexcept { return m_peak; }

    // Adds `frames` samples (mono) to `out`.
    void render(float* out, int frames, const StringControls& controls) noexcept;

private:
    void design(const StringControls& c) noexcept;
    float read(double delay) const noexcept;       // y[n - delay], delay >= 2

    std::vector<float> m_line;
    int m_mask = 0;
    int m_write = 0;

    bool m_active = false;
    bool m_released = false;
    int m_openKey = 28;
    NoteKind m_kind = NoteKind::Finger;
    double m_pitch = 40.0, m_target = 40.0, m_slideStep = 0.0;   // semitones per sample

    // The loop as designed for the present pitch.
    double m_delay = 100.0;                        // read point, samples
    double m_gain = 0.99, m_lossCoef = 0.0, m_lossState = 0.0;
    double m_allpassCoef = 0.0, m_allpassIn = 0.0, m_allpassOut = 0.0;
    double m_period = 100.0;
    PickupTap m_neck, m_bridge;

    double m_fade = 1.0, m_fadeStep = 0.0;         // per sample, while fading out
    double m_peak = 0.0;                           // the output's recent peak
    int m_quietFrames = 0;
    Exciter m_exciter;
};

} // namespace bass
