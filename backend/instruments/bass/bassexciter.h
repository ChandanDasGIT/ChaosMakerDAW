#pragma once

#include <cstdint>

// How a string is set going: the shape fed into the string loop, and the click a slap or pop
// adds straight to the output (the string hitting the frets, the snap of a popped string).
//
//   Finger   a round bump a few ms wide (narrower, so brighter, when played hard)
//   Slap     the thumb: a very short, strong hit, plus a fret click
//   Pop      a finger pulls the string out and lets it snap back: short, strong, a sharper click
//   Muted    a wide, soft bump: the palm takes the edge off
//
// The bump goes through a comb for where the string is struck: struck at a fraction b of its
// length, every 1/b-th overtone is missing (plucking by the bridge is bright, by the neck round).
namespace bass {

enum class NoteKind { Finger, Slap, Pop, Muted };

class Exciter {
public:
    // strength 0..1 (the velocity's level), brightness 0..1 (narrower bumps), strikeAt: the
    // fraction of the string's length from the bridge, period: the string's period in samples.
    void start(NoteKind kind, double strength, double brightness, double strikeAt, double period,
               double sampleRate) noexcept;
    void stop() noexcept { m_left = 0; }
    bool active() const noexcept { return m_left > 0; }

    // One sample: what goes into the string, and what goes straight to the output.
    void next(float& toString, float& direct) noexcept;

private:
    double bump(double n) const noexcept;          // the shape at sample n (0 outside)
    float noise() noexcept;

    double m_width = 1.0;                          // the bump, in samples
    double m_amplitude = 0.0;
    double m_combDelay = 0.0;                      // the strike position, in samples
    double m_n = 0.0;
    int m_left = 0;                                // samples still to go

    double m_clickLevel = 0.0, m_clickDecay = 0.0; // per sample
    double m_clickHp = 0.0, m_clickLp = 0.0;       // filter coefficients
    double m_hpState = 0.0, m_lpState = 0.0;
    std::uint32_t m_seed = 0x2545F491u;
};

} // namespace bass
