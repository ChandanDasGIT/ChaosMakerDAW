#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

// Test songs for the chord analyzer (restMap G21): bars of strummed chords with a click on
// every beat, rendered here so the tests need no sound files.
namespace chordrender {

constexpr double rate = 44100.0;

struct Bar {
    int root;                              // pitch class
    std::vector<int> tones;                // above the root, semitones
};

inline double hzOf(double midiNote, double a4) { return a4 * std::pow(2.0, (midiNote - 69.0) / 12.0); }

// A song: each bar's chord strummed on every beat (a bass note and the chord, plucked strings
// that die away) with a click on every beat and a louder one on the bar line.
inline std::vector<float> song(const std::vector<Bar>& bars, double bpm, double a4 = 440.0, int beatsPerBar = 4)
{
    const double beat = 60.0 / bpm;
    std::vector<float> out(std::size_t((double(bars.size()) * beatsPerBar * beat + 1.0) * rate), 0.0f);
    std::uint32_t seed = 7;
    for (std::size_t b = 0; b < bars.size(); ++b)
        for (int k = 0; k < beatsPerBar; ++k) {
            const double at = 0.3 + (double(b) * beatsPerBar + k) * beat;
            const auto from = std::size_t(at * rate);
            std::vector<double> notes{36.0 + bars[b].root};
            for (int tone : bars[b].tones) notes.push_back(48.0 + bars[b].root + tone);
            for (std::size_t i = 0; i < std::size_t(beat * rate) && from + i < out.size(); ++i) {
                const double t = double(i) / rate;
                double v = 0.0;
                for (double note : notes)
                    for (int h = 1; h <= 5; ++h) v += std::sin(2.0 * std::numbers::pi * hzOf(note, a4) * h * t) / (h * h);
                out[from + i] += float(0.08 * v * std::exp(-2.0 * t));
            }
            for (std::size_t i = 0; i < std::size_t(0.012 * rate) && from + i < out.size(); ++i) {
                seed = seed * 1664525u + 1013904223u;
                out[from + i] += (k == 0 ? 0.5f : 0.3f) * (float(seed >> 8) / float(1u << 24) * 2.0f - 1.0f);
            }
        }
    return out;
}

inline const std::vector<int> major{0, 4, 7}, minor{0, 3, 7}, seventh{0, 4, 7, 10};

inline std::vector<Bar> repeated(const std::vector<Bar>& bars, int times)
{
    std::vector<Bar> out;
    for (int i = 0; i < times; ++i) out.insert(out.end(), bars.begin(), bars.end());
    return out;
}

} // namespace chordrender
