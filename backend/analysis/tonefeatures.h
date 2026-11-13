#pragma once

#include <array>
#include <vector>

// What a guitar tone measures like (restMap G19): the things the tone analyzer compares
// between a reference recording and a pedalboard's output.
//
//   balance  the level in third-octave bands from 80 Hz to 8 kHz, each against the mean of
//            them all: the tone's colour, whatever its loudness;
//   crest    peak over RMS in 50 ms windows, the median: how squashed the sound is. A clean
//            guitar is spiky (12 dB and up), a crunch about 9, high gain 6 to 8;
//   chords   the share of the time three or more note names sound at once: chords against
//            single notes (a solo, a riff);
//   tail     how long the sound takes to fall 30 dB where the playing stops (a second with
//            no new note): a dry guitar stops at once, a reverb or delay rings on. Unknown
//            when it never stops.
//
// Only the parts with sound in them count (quieter than 45 dB under the loud parts: skipped).
namespace analysis {

struct ToneFeatures {
    static constexpr int Bands = 21;                       // 80 Hz .. 8 kHz
    static double bandHz(int band) noexcept;               // the band's centre

    std::array<double, Bands> balanceDb{};
    double crestDb = 0.0;
    double levelDb = -120.0;                               // RMS of the parts with sound, dBFS
    double chordShare = 0.0;                               // 0 .. 1
    double tailSeconds = -1.0;                             // -1: never stopped, not known
    double soundSeconds = 0.0;                             // how much of it had sound
    bool valid = false;                                    // false: silence, or under a second of sound
};

// withNotes false: chordShare and tailSeconds are left as they are (the search measures
// hundreds of renders and needs only the balance, the crest and the level).
ToneFeatures measureTone(const std::vector<float>& mono, double sampleRate, bool withNotes = true);

// How far two tones' colours are apart: the RMS of the band differences, in dB, over the bands
// from `fromHz` to `toHz`, each tone's bands first set against its own mean in that range.
double balanceDistance(const ToneFeatures& a, const ToneFeatures& b, double fromHz = 100.0, double toHz = 6400.0);

} // namespace analysis
