#pragma once

#include <array>
#include <string_view>

// The Chords track's autoplay patterns: what one bar of a held chord plays, for any time
// signature. Positions are in beats (quarter notes) from the bar's start, so a pattern follows
// the project's BPM by itself; 3/4, 6/8 and other meters get patterns of their own (a waltz is
// not a stretched 4/4). Built without heap memory: the chord player may rebuild one on the
// audio thread when the time signature changes.
namespace chords {

enum class Part { Chord, Bass };          // a strip's upper area (both hands) or lower (bass only)

constexpr int AutoplayOff = 0;            // the chord is held while pressed
constexpr int AutoplayModes = 5;          // Off, 1 .. 4

// What a step plays.
constexpr int VoiceChord = -1;            // the whole right-hand voicing
constexpr int VoiceBass = -2;             // the bass note
constexpr int VoiceBassOctave = -3;       // the bass note an octave up
// 0 and up: one note of the voicing; past its last note it goes on an octave higher.

struct PatternStep {
    double beat = 0.0;                    // from the bar's start, in quarter notes
    double length = 0.5;                  // in quarter notes
    int voice = VoiceChord;
    int velocity = 100;                   // 1 .. 127, before the press's own velocity
};

constexpr int MaxSteps = 96;

struct Pattern {
    std::array<PatternStep, MaxSteps> steps{};
    int count = 0;
    double barBeats = 4.0;
};

// One bar of autoplay `mode` (1 .. 4) under numerator/denominator, for the strip's chord area
// or bass area. Mode 0 (or any other) gives an empty pattern.
//   1 Block     chords on the pulse, bass on the strong beats (3/4: oom-pah-pah; 6/8: rolling)
//   2 Ballad    the chord and bass on one, then the chord broken in eighths
//   3 Arpeggio  the chord's notes rising in eighths over a held bass
//   4 Arpeggio 16ths  up and down in sixteenths, a bar and a half wide
void buildPattern(int mode, int numerator, int denominator, Part part, Pattern& out) noexcept;

// "Off", "Block", "Ballad", "Arpeggio", "Arpeggio 16ths".
std::string_view autoplayName(int mode) noexcept;

} // namespace chords
