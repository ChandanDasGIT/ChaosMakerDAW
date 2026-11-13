#include "analysis/dryguitar.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace analysis {
namespace {

constexpr double LevelDb = -30.0;          // RMS of the sounding parts, like a guitar plugged straight in
constexpr double PickAt = 0.14;            // where the pick strikes, as a part of the string from the bridge
constexpr double PickupAt = 0.17;          // the pickup's place: between a bridge and a middle pickup
constexpr double PickupHz = 4800.0, PickupQ = 1.6;   // the pickup's resonance with the cable
constexpr double BodyHz = 170.0;           // from here up the overtones fall off,
constexpr double TopHz = 2000.0;           // and from here faster still

struct Note {
    double at;                             // seconds
    double hz;
    double seconds;                        // until it is stopped
    double strength;                       // 0 .. 1
    bool muted;                            // palm muted: dull and short
};

double hzOf(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

// One plucked string added into `out`.
void pluck(std::vector<float>& out, double rate, const Note& note, std::uint32_t& seed)
{
    const double length = rate / note.hz;
    const int whole = int(length);
    if (whole < 4) return;
    const double fraction = length - whole;
    std::vector<double> line(std::size_t(whole + 2), 0.0);
    // The pluck: noise shaped to a dull click, minus itself delayed by the pick's place.
    const int pick = std::max(1, int(std::lround(PickAt * length)));
    std::vector<double> burst(std::size_t(whole + 2), 0.0);
    double low = 0.0;
    const double brightness = note.muted ? 0.12 : 0.25 + 0.35 * note.strength;
    for (int i = 0; i < whole; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const double white = double(seed >> 8) / double(1u << 24) * 2.0 - 1.0;
        low += brightness * (white - low);
        burst[std::size_t(i)] = low;
    }
    for (int i = 0; i < whole; ++i) line[std::size_t(i)] = burst[std::size_t(i)] - (i >= pick ? burst[std::size_t(i - pick)] : 0.0);

    // The loop: higher notes and muted strings die sooner; the highs die first.
    const double ring = note.muted ? 0.18 : std::clamp(4.5 * std::pow(110.0 / note.hz, 0.4), 1.2, 5.0);   // seconds to fall 60 dB
    const double loss = std::pow(10.0, -3.0 / (ring * note.hz));
    const double damp = note.muted ? 0.55 : 0.18;
    const int pickup = std::max(1, int(std::lround(PickupAt * length)));
    const auto from = std::size_t(note.at * rate);
    const auto frames = std::size_t(std::min(note.seconds + 0.05, 6.0) * rate);
    const auto release = std::size_t(note.seconds * rate);
    std::size_t at = 0;
    double state = 0.0, gain = note.strength;
    std::vector<double> recent(std::size_t(pickup + 1), 0.0);
    std::size_t recentAt = 0;
    for (std::size_t i = 0; i < frames && from + i < out.size(); ++i) {
        const std::size_t next = (at + 1) % std::size_t(whole + 1);
        const double sample = line[at] * (1.0 - fraction) + line[next] * fraction;
        state += (1.0 - damp) * (sample - state);
        line[at] = state * loss;
        at = next;
        // The pickup hears the string minus itself a little later.
        recent[recentAt] = sample;
        const double heard = sample - recent[(recentAt + 1) % recent.size()];
        recentAt = (recentAt + 1) % recent.size();
        if (i >= release) gain *= std::exp(-1.0 / (0.012 * rate));   // the finger stops it
        out[from + i] += float(gain * heard);
    }
}

// The pickup's resonance: a low-pass with a bump, then the level.
void finish(std::vector<float>& out, double rate)
{
    const double w = 2.0 * std::numbers::pi * PickupHz / rate, alpha = std::sin(w) / (2.0 * PickupQ), c = std::cos(w);
    const double a0 = 1.0 + alpha, b0 = (1.0 - c) / 2.0 / a0, b1 = (1.0 - c) / a0, b2 = b0, a1 = -2.0 * c / a0, a2 = (1.0 - alpha) / a0;
    // A plucked string's overtones fall off steadily (6 dB an octave from BodyHz up, 12 from
    // TopHz): the loop above leaves them all alike. Set by ear and measure against real
    // guitars plugged straight in.
    const double body = 1.0 - std::exp(-2.0 * std::numbers::pi * BodyHz / rate);
    const double top = 1.0 - std::exp(-2.0 * std::numbers::pi * TopHz / rate);
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0, sum = 0.0, tilt = 0.0, tilt1 = 0.0;
    for (float& v : out) {
        tilt1 += body * (v - tilt1);
        tilt += top * (tilt1 - tilt);
        const double y = b0 * tilt + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = tilt; y2 = y1; y1 = y;
        v = float(y);
    }
    // The level of the parts with sound.
    std::size_t counted = 0;
    float peak = 0.0f;
    for (float v : out) peak = std::max(peak, std::abs(v));
    for (float v : out)
        if (std::abs(v) > 0.003f * peak) { sum += double(v) * v; ++counted; }
    if (counted == 0) return;
    const double scale = std::pow(10.0, LevelDb / 20.0) / std::sqrt(sum / double(counted));
    for (float& v : out) v = float(v * scale);
}

void strum(std::vector<Note>& notes, double at, std::initializer_list<int> midi, double seconds, double strength, bool up = false, bool muted = false)
{
    std::vector<int> order(midi);
    if (up) std::reverse(order.begin(), order.end());
    double t = at;
    for (int note : order) {
        notes.push_back({t, hzOf(note), seconds - (t - at), strength, muted});
        t += muted ? 0.004 : 0.011;
    }
}

std::vector<float> render(const std::vector<Note>& notes, double rate)
{
    std::vector<float> out(std::size_t(DryGuitarSeconds * rate), 0.0f);
    std::uint32_t seed = 0x1234567u;
    for (const auto& note : notes) pluck(out, rate, note, seed);
    finish(out, rate);
    return out;
}

} // namespace

std::vector<float> dryGuitarChords(double rate)
{
    if (!(rate > 8000.0)) return {};
    std::vector<Note> notes;
    // Open chords, strummed down and up: E, A, D, G (half a second each stroke).
    const std::initializer_list<int> open[4] = {{40, 47, 52, 56, 59, 64}, {45, 52, 57, 61, 64}, {50, 57, 62, 66}, {43, 47, 50, 55, 59, 67}};
    double t = 0.05;
    for (int c = 0; c < 4; ++c) {
        strum(notes, t, open[c], 0.36, 0.9);
        strum(notes, t + 0.375, open[c], 0.34, 0.6, true);
        t += 0.75;
    }
    // Power chords, muted eighths with an open one to end each bar: E5, G5, A5, D5.
    const std::initializer_list<int> power[4] = {{40, 47, 52}, {43, 50, 55}, {45, 52, 57}, {38, 45, 50}};
    for (int c = 0; c < 4; ++c) {
        for (int k = 0; k < 3; ++k) strum(notes, t + 0.18 * k, power[c], 0.15, 0.85, false, true);
        strum(notes, t + 0.54, power[c], 0.17, 1.0);
        t += 0.72;
    }
    return render(notes, rate);
}

std::vector<float> dryGuitarSolo(double rate)
{
    if (!(rate > 8000.0)) return {};
    // Single notes up and down the A minor pentatonic, from the low strings to high on the neck.
    constexpr int line[] = {45, 48, 50, 52, 55, 57, 60, 62, 64, 67, 69, 72, 74, 76, 72, 69, 67, 64, 62, 60, 57, 64, 69, 76, 79, 76, 72, 69};
    constexpr double lengths[] = {0.2, 0.2, 0.2, 0.2, 0.2, 0.2, 0.2, 0.2, 0.2, 0.2, 0.2, 0.2, 0.2, 0.42, 0.14, 0.14, 0.14, 0.14, 0.14, 0.14,
                                  0.3, 0.2, 0.2, 0.2, 0.45, 0.15, 0.15, 0.4};
    std::vector<Note> notes;
    double t = 0.05;
    int i = 0;
    for (int midi : line) {
        notes.push_back({t, hzOf(midi), lengths[i] - 0.01, (i % 4 == 0) ? 1.0 : 0.75, false});
        t += lengths[i++];
    }
    return render(notes, rate);
}

} // namespace analysis
