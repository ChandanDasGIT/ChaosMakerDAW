#pragma once

#include "dsp/biquad.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

// Follows the pitch of one voice or one melody line (restMap G18), every 10 ms. The same code
// runs live (push, real-time safe) and on recorded audio (analyse).
//
// The sound is low-passed (4-pole, at 40 % of the new rate) and decimated to 16-32 kHz, then
// YIN looks for the period: the cumulative-mean-normalised difference of the last
// Window + MaxLag samples, the first dip under Threshold, refined by a parabola. A frame is
// voiced when that dip is under Threshold and the sound is above MinLevelDb. Clarity is
// 1 - the dip (1 = a clean periodic tone). Chords and full mixes have no single period: the
// result there jumps around.
namespace pitch {

constexpr double MinHz = 60.0;            // a low male voice, a bass guitar's A string
constexpr double MaxHz = 1500.0;          // above a soprano's high notes
constexpr double HopSeconds = 0.01;
constexpr float Threshold = 0.2f;
constexpr float MinLevelDb = -55.0f;

struct Frame {
    std::int64_t sample = 0;              // the frame's centre, in input samples since reset
    float hz = 0.0f;                      // 0: no pitch (silence, noise, breath)
    float clarity = 0.0f;                 // 0..1
    float levelDb = -120.0f;              // RMS of the frame, dBFS
};

// MIDI note number (fractional) of a frequency, A4 = 440 Hz = 69.
double midiOf(double hz) noexcept;

class Tracker {
public:
    // Allocates: not on the audio thread.
    void prepare(double sampleRate);
    void reset() noexcept;

    // Adds samples; calls emit(const Frame&) for each finished frame. Real-time safe.
    template <class Emit>
    void push(const float* in, int frames, Emit&& emit) noexcept
    {
        for (int i = 0; i < frames; ++i) {
            double x = in[i];
            for (int s = 0; s < 2; ++s) x = biquad::process(m_lowPass, m_state[std::size_t(s)], x);
            if (++m_phase < m_decimation) continue;
            m_phase = 0;
            m_buffer[std::size_t(m_write)] = float(x);
            m_write = (m_write + 1) % int(m_buffer.size());
            ++m_decimated;
            if (m_decimated >= std::int64_t(m_buffer.size()) && ++m_sinceHop >= m_hop) {
                m_sinceHop = 0;
                emit(analyseFrame());
            }
        }
    }

    // A whole recording, cleaned up: voiced runs shorter than 30 ms are dropped and single
    // frames an octave off their neighbours are moved back (YIN's usual slips).
    // `cancel`: checked every second of audio; set, the result is empty.
    static std::vector<Frame> analyse(std::span<const float> mono, double sampleRate,
                                      const std::atomic<bool>* cancel = nullptr);
    static void cleanUp(std::vector<Frame>& frames);

    int decimation() const noexcept { return m_decimation; }

private:
    Frame analyseFrame() noexcept;

    biquad::Coefficients m_lowPass;
    std::array<biquad::State, 2> m_state{};
    int m_decimation = 4, m_phase = 0;
    double m_rate = 12000.0;              // after decimation
    int m_window = 200, m_maxLag = 200, m_minLag = 8, m_hop = 120, m_sinceHop = 0;
    std::vector<float> m_buffer;          // a ring of the last m_window + m_maxLag samples
    std::vector<float> m_frame;           // the ring in order, for one analysis
    std::vector<float> m_difference;      // normalised (YIN's d')
    std::vector<double> m_raw;            // d(tau), for the parabola
    int m_write = 0;
    std::int64_t m_decimated = 0;
};

} // namespace pitch
