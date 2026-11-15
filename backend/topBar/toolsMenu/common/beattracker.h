#pragma once

#include <vector>

// The tempo and the beats of a recording (restMap G20, reused by the BPM analyzer G22), from an
// onset envelope. One steady tempo: the beats are phase + k * period frames, fitted to the whole
// recording at once (a song played to a click; a live one that drifts gets an average).
//
// 1. Candidate periods from the envelope's autocorrelation, each scored with its multiples
//    (a beat repeats every 1, 2, 3, 4 periods; a multiple counts only as far as the period
//    itself repeats) and a gentle preference for 120 BPM, so a beat
//    and its double or half are told apart the way most music is counted.
// 2. The best period and phase fitted as a comb laid over the envelope: coarse, then fine.
// 3. A tempo within 0.12 BPM of a whole number is taken as that number (most produced music).
namespace analysis {

struct Beat {
    double frameRate = 100.0;
    double period = 50.0;                  // frames per beat
    double phase = 0.0;                    // the frame of a beat, 0 <= phase < period
    double strength = 0.0;                 // how well the comb fits (the envelope's mean on the beats)

    double bpm() const noexcept { return 60.0 * frameRate / period; }
    double frameOf(int k) const noexcept { return phase + k * period; }
    double secondsOf(int k) const noexcept { return frameOf(k) / frameRate; }
};

constexpr double MinBpm = 40.0;
constexpr double MaxBpm = 200.0;
constexpr double PreferredBpm = 120.0;

// nearBpm > 0: only tempos within 5 % of it (to double or halve a found tempo). A default Beat
// (strength 0) when the envelope is too short or silent.
Beat trackBeats(const std::vector<float>& envelope, double frameRate, double nearBpm = 0.0);

// The envelope with its slow rise and fall taken out (what the tracker fits), for the meter.
std::vector<float> beatEnvelope(const std::vector<float>& envelope, double frameRate);

} // namespace analysis
