#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// The Chords track's autoplay patterns: what a held chord plays, two bars at a time, for any
// time signature. Each pattern is written out for the meters a pianist counts differently
// (4/4, 3/4, 6/8, 7/8 = 2+2+3 and 5/4 = 3+2); other meters borrow the nearest of them (2/4 and
// 8/8 from 4/4, 9/8 and 12/8 from 6/8, 5/8 = 3+2 from 6/8, 6/4 from 3/4 ...). Bar 2 is a
// variation of bar 1, so the pattern never loops a single bar.
//
// Positions are in beats (quarter notes) from the pattern's start, so a pattern follows the
// project's BPM by itself. Built without heap memory: the chord player may rebuild one on the
// audio thread when the time signature changes.
namespace chords {

// A strip's chord area with both hands, its bass area (the left hand only), or its chord area
// with the right hand only (bass with chords switched off: where the left hand played alone,
// the right hand plays that chord tone instead, so the rhythm stays).
enum class Part { Chord, Bass, RightHand };

constexpr int AutoplayOff = 0;            // the chord is held while pressed
constexpr int AutoplayModes = 5;          // Off, 1 .. 4

// How loud the notes are: all the same, the chord's top note louder, or by the meter (the bar's
// one loudest, then the group starts, the beats, the off-beat eighths, the sixteenths).
constexpr int DynamicsEven = 0;
constexpr int DynamicsTopNote = 1;
constexpr int DynamicsMeter = 2;
constexpr int DynamicsModes = 3;

// The meter accents' velocities.
constexpr int AccentOne = 104;            // the bar's first beat
constexpr int AccentGroup = 96;           // a group start (4/4: beat 3; 6/8: the second dotted beat)
constexpr int AccentBeat = 84;            // another beat of x/4
constexpr int AccentPulse = 76;           // another eighth of x/8
constexpr int AccentOffbeat = 74;         // an off-beat eighth of x/4
constexpr int AccentSixteenth = 64;       // anything finer

// What a step plays. The right hand's chord tones are counted from the chord's root in
// E3 .. D#4: degree 0 the root, 1 the third, 2 the fifth (the seventh of a
// ninth chord, which has no fifth), 3 the seventh; a degree past the chord's last tone goes on
// an octave higher (a triad's degree 3 is the octave).
enum class Kind : std::uint8_t {
    Chord,                                // the whole right-hand voicing
    Upper,                                // the voicing without its lowest note
    Roll,                                 // the voicing low to high, `spreadMs` apart
    Tone,                                 // right-hand chord tone `degree`, `octave` octaves up
    Bass,                                 // the bass note, `octave` octaves up
    Low,                                  // left hand: chord tone `degree` just above the bass, `octave` up
};

struct PatternStep {
    double beat = 0.0;                    // from the pattern's start, in quarter notes
    double length = 0.5;                  // in quarter notes
    Kind kind = Kind::Chord;
    int degree = 0;
    int octave = 0;
    int velocity = 100;                   // 1 .. 127, before the press's own velocity
    int accent = AccentBeat;              // its velocity by the meter (Accent*), for DynamicsMeter
    double nudgeMs = 0.0;                 // played this much late (early if negative)
    double spreadMs = 0.0;                // Roll: between one note and the next
};

constexpr int MaxSteps = 256;
constexpr int PatternBars = 2;

struct Pattern {
    std::array<PatternStep, MaxSteps> steps{};
    int count = 0;
    double barBeats = 4.0;                // one bar of the meter
    int bars = PatternBars;
    bool pedal = false;                   // the sustain pedal is changed on every chord
    double lengthBeats() const noexcept { return barBeats * bars; }
};

// Two bars of autoplay `mode` (1 .. 4) under numerator/denominator, for the strip's chord area
// or bass area (the bass area keeps the left hand's notes only). Mode 0 (or any other) gives an
// empty pattern.
//   1 Flowing   a broken chord across two octaves over a held bass, in eighths, pedalled
//   2 Pop       a syncopated sixteenth arpeggio in a shape (4/4: 3+3+3+3+2+2), bass on 1 and 2&
//   3 Comping   rhythmic chords with pushes over a moving bass
//   4 Ballad    a rolled chord, then a slow top line of chord tones, pedalled
// The left hand plays only the bass note, the third, the fifth and the octave (C2 E2 G2 C3 for
// C), and every pattern uses all four across its two bars. Group starts of the meter carry accents; weak sixteenths are played a few ms early or late
// (always the same, so a recording replays as it was heard).
void buildPattern(int mode, int numerator, int denominator, Part part, Pattern& out) noexcept;

// "Off", "Flowing", "Pop", "Comping", "Ballad".
std::string_view autoplayName(int mode) noexcept;
// One line on how mode `mode` plays, for the page's tooltips ("" for none).
std::string_view autoplayDescription(int mode) noexcept;

// Where the meter's groups start, in beats from the bar's start (4/4: 0 2; 3/4: 0; 6/8: 0 1.5;
// 7/8: 0 1 2; 5/4: 0 3). Returns how many were written to `out`.
int groupStarts(int numerator, int denominator, std::array<double, 32>& out) noexcept;

} // namespace chords
