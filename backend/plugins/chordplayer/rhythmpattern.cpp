#include "plugins/chordplayer/rhythmpattern.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace chords {
namespace {

// ---------------------------------------------------------------------------------------------
// The written patterns. One entry per note (or chord) of one bar, beats from the bar's start.
// Hold: the note rings to the end of the bar.

constexpr double Hold = -1.0;

struct Src {
    double beat;
    double length;
    Kind kind;
    int degree;
    int octave;
    int velocity;
    int spreadMs;
};

constexpr Src C(double b, double l, int v) { return {b, l, Kind::Chord, 0, 0, v, 0}; }
constexpr Src U(double b, double l, int v) { return {b, l, Kind::Upper, 0, 0, v, 0}; }
constexpr Src Rl(double b, double l, int v, int spreadMs) { return {b, l, Kind::Roll, 0, 0, v, spreadMs}; }
constexpr Src T(double b, double l, int degree, int octave, int v) { return {b, l, Kind::Tone, degree, octave, v, 0}; }
constexpr Src B(double b, double l, int v, int octave = 0) { return {b, l, Kind::Bass, 0, octave, v, 0}; }
constexpr Src L(double b, double l, int degree, int v, int octave = 0) { return {b, l, Kind::Low, degree, octave, v, 0}; }

constexpr double S = 0.22;                // a sixteenth's sounding length

// ---- 4/4 ------------------------------------------------------------------------------------
// 1 Flowing: eighths grouped 3+3+2 over a held bass.
constexpr Src Flow44A[]{B(0, Hold, 90), L(0.5, Hold, 1, 66), T(1.0, Hold, 0, 0, 70), T(1.5, Hold, 1, 0, 78),
                        T(2.0, Hold, 2, 0, 68), T(2.5, Hold, 0, 1, 70), T(3.0, Hold, 1, 1, 78), T(3.5, Hold, 2, 0, 64)};
constexpr Src Flow44B[]{B(0, Hold, 90), L(0.5, Hold, 2, 66), L(1.0, Hold, 0, 68, 1), T(1.5, Hold, 1, 0, 78),
                        T(2.0, Hold, 2, 0, 68), T(2.5, Hold, 0, 1, 72), T(3.0, Hold, 2, 1, 80), T(3.5, Hold, 1, 1, 66)};
// 2 Pop: sixteenths in cells of 3+3+3+3+2+2, bass on 1 and the "and" of 2.
constexpr Src Pop44A[]{B(0, 1.4, 92), L(1.5, 2.4, 2, 84),
                       T(0.00, S, 0, 0, 86), T(0.25, S, 2, 0, 64), T(0.50, S, 0, 1, 70),
                       T(0.75, S, 1, 0, 84), T(1.00, S, 2, 0, 64), T(1.25, S, 0, 1, 70),
                       T(1.50, S, 2, 0, 84), T(1.75, S, 0, 1, 66), T(2.00, S, 1, 1, 72),
                       T(2.25, S, 0, 1, 84), T(2.50, S, 2, 0, 64), T(2.75, S, 1, 0, 66),
                       T(3.00, S, 2, 0, 84), T(3.25, S, 0, 1, 66),
                       T(3.50, S, 1, 1, 86), T(3.75, S, 0, 1, 64)};
constexpr Src Pop44B[]{B(0, 1.4, 92), L(1.5, 1.4, 1, 84), B(3.0, 0.45, 78, 1), L(3.5, 0.45, 2, 76),
                       T(0.00, S, 0, 0, 86), T(0.25, S, 1, 0, 64), T(0.50, S, 2, 0, 70),
                       T(0.75, S, 0, 1, 84), T(1.00, S, 2, 0, 64), T(1.25, S, 1, 0, 66),
                       T(1.50, S, 2, 0, 84), T(1.75, S, 0, 1, 66), T(2.00, S, 1, 1, 72),
                       T(2.25, S, 2, 1, 86), T(2.50, S, 1, 1, 66), T(2.75, S, 0, 1, 68),
                       T(3.00, S, 2, 0, 84), T(3.25, S, 1, 0, 64),
                       T(3.50, S, 2, 0, 82), T(3.75, S, 0, 1, 66)};
// 3 Comping: chords on 1, the push on 2&, 3& and 4, a bass walking root - third - fifth - octave.
constexpr Src Comp44A[]{B(0, 1.4, 92), L(2.0, 0.9, 2, 82), B(3.0, 0.45, 80, 1), L(3.5, 0.4, 2, 74),
                        C(0, 1.3, 94), C(1.5, 0.45, 86), U(2.5, 0.4, 72), C(3.0, 0.45, 84), U(3.75, 0.2, 58)};
constexpr Src Comp44B[]{B(0, 1.4, 92), L(1.5, 0.4, 1, 78), L(2.0, 0.9, 2, 84), B(3.0, 0.45, 80, 1), L(3.5, 0.45, 2, 80),
                        C(0, 0.9, 92), U(1.5, 0.4, 76), C(2.0, 0.9, 86), U(2.75, 0.2, 58), C(3.5, 0.45, 88)};
// 4 Ballad: the chord rolled over the bass, then a slow top line.
constexpr Src Ball44A[]{B(0, Hold, 88), Rl(0, Hold, 82, 35), L(2.0, Hold, 2, 62),
                        T(1.0, 1.0, 2, 1, 70), T(2.0, 1.0, 1, 1, 74), T(3.0, 0.5, 2, 1, 66), T(3.5, 0.5, 0, 2, 68)};
constexpr Src Ball44B[]{B(0, Hold, 86), U(0, 2.0, 68), L(1.0, 1.0, 1, 62), B(2.0, Hold, 70, 1), T(1.5, 0.5, 2, 1, 66), Rl(2.0, Hold, 72, 30),
                        T(2.5, 1.0, 1, 1, 70), T(3.5, 0.5, 0, 1, 62)};

// ---- 3/4 ------------------------------------------------------------------------------------
// 1 Flowing: six eighths in three pairs, each beat starting low again.
constexpr Src Flow34A[]{B(0, Hold, 90), L(0.5, Hold, 2, 66), T(1.0, Hold, 0, 0, 72), T(1.5, Hold, 2, 0, 70),
                        T(2.0, Hold, 1, 0, 74), T(2.5, Hold, 0, 1, 64)};
constexpr Src Flow34B[]{B(0, Hold, 90), L(0.5, Hold, 1, 66), L(1.0, Hold, 0, 72, 1), T(1.5, Hold, 2, 0, 70),
                        T(2.0, Hold, 1, 0, 74), T(2.5, Hold, 0, 1, 64)};
// 2 Pop: a waltz: bass and a sixteenth run on 1, the chord on 2 and 3 with sixteenths after.
constexpr Src Pop34A[]{B(0, 0.95, 92), L(2.5, 0.45, 2, 76),
                       T(0.00, S, 0, 0, 86), T(0.25, S, 1, 0, 64), T(0.50, S, 2, 0, 70), T(0.75, S, 0, 1, 64),
                       U(1.0, 0.4, 80), T(1.50, S, 2, 0, 70), T(1.75, S, 0, 1, 64),
                       U(2.0, 0.4, 80), T(2.50, S, 1, 1, 70), T(2.75, S, 0, 1, 64)};
constexpr Src Pop34B[]{B(0, 0.95, 92), L(1.0, 0.45, 1, 80), B(2.0, 0.45, 82, 1),
                       T(0.00, S, 0, 0, 86), T(0.25, S, 2, 0, 64), T(0.50, S, 0, 1, 70), T(0.75, S, 1, 1, 64),
                       U(1.0, 0.4, 80), T(1.50, S, 2, 0, 70), T(1.75, S, 1, 0, 64),
                       U(2.0, 0.4, 80), T(2.50, S, 0, 1, 70), T(2.75, S, 2, 0, 64)};
// 3 Comping: oom-pah-pah, the second bar with a fifth in the bass and a pickup.
constexpr Src Comp34A[]{B(0, 0.9, 92), U(1.0, 0.45, 80), U(2.0, 0.45, 78), L(2.5, 0.4, 1, 74)};
constexpr Src Comp34B[]{B(0, 0.9, 92), U(1.0, 0.45, 80), L(2.0, 0.45, 2, 80), B(2.5, 0.4, 82, 1), U(2.0, 0.4, 76), C(2.5, 0.45, 86)};
// 4 Ballad.
constexpr Src Ball34A[]{B(0, Hold, 88), Rl(0, Hold, 82, 35), L(1.0, Hold, 2, 64), T(1.0, 1.0, 2, 1, 68), T(2.0, 1.0, 1, 1, 72)};
constexpr Src Ball34B[]{B(0, Hold, 86), U(0, 1.0, 66), L(1.0, 1.0, 1, 62), L(2.0, Hold, 0, 66, 1), T(1.0, 1.5, 0, 2, 70), T(2.5, 0.5, 2, 1, 64)};

// ---- 6/8 ------------------------------------------------------------------------------------
// 1 Flowing: two rolling groups of three, the first in the left hand.
constexpr Src Flow68A[]{B(0, Hold, 90), L(0.5, Hold, 2, 64), L(1.0, Hold, 0, 66, 1), T(1.5, Hold, 1, 0, 80),
                        T(2.0, Hold, 2, 0, 68), T(2.5, Hold, 0, 1, 70)};
constexpr Src Flow68B[]{B(0, Hold, 90), L(0.5, Hold, 1, 64), T(1.0, Hold, 0, 0, 68), T(1.5, Hold, 2, 0, 80),
                        T(2.0, Hold, 0, 1, 70), T(2.5, Hold, 1, 1, 72)};
// 2 Pop: two lilting groups of three eighths, each starting low, the bass on 1 and 4.
constexpr Src Pop68A[]{B(0, 1.4, 92), L(1.5, 1.4, 2, 84),
                       T(0.00, S, 0, 0, 86), T(0.25, S, 2, 0, 62), T(0.50, S, 0, 1, 68), T(1.00, S, 1, 0, 70), T(1.25, S, 2, 0, 62),
                       T(1.50, S, 0, 0, 84), T(1.75, S, 2, 0, 62), T(2.00, S, 1, 1, 68), T(2.50, S, 0, 1, 70), T(2.75, S, 2, 0, 62)};
constexpr Src Pop68B[]{B(0, 1.4, 92), L(1.5, 0.9, 1, 84), B(2.5, 0.45, 76, 1),
                       T(0.00, S, 0, 0, 86), T(0.25, S, 1, 0, 62), T(0.50, S, 2, 0, 68), T(1.00, S, 0, 1, 70), T(1.25, S, 2, 0, 62),
                       T(1.50, S, 1, 0, 84), T(1.75, S, 2, 0, 62), T(2.00, S, 0, 1, 68), T(2.50, S, 1, 1, 70), T(2.75, S, 0, 1, 62)};
// 3 Comping: chords on eighths 1, 3, 4 and 6.
constexpr Src Comp68A[]{B(0, 0.9, 92), C(0, 0.9, 90), U(1.0, 0.4, 72), L(1.5, 0.9, 2, 84), C(1.5, 0.45, 84),
                        U(2.5, 0.4, 72)};
constexpr Src Comp68B[]{B(0, 0.9, 92), C(0, 0.9, 90), U(1.0, 0.4, 72), L(1.5, 0.4, 1, 82), U(1.5, 0.4, 80),
                        U(2.0, 0.4, 70), C(2.5, 0.45, 84), B(2.5, 0.45, 76, 1)};
// 4 Ballad.
constexpr Src Ball68A[]{B(0, Hold, 88), Rl(0, Hold, 82, 35), L(1.5, Hold, 2, 64), T(1.5, 1.0, 1, 1, 72), T(2.5, 0.5, 2, 1, 64)};
constexpr Src Ball68B[]{B(0, Hold, 86), U(0, 1.5, 66), L(1.0, 0.5, 1, 62), B(1.5, Hold, 64, 1), T(1.5, 1.0, 2, 1, 70), T(2.5, 0.5, 0, 2, 64)};

// ---- 7/8 = 2+2+3 ----------------------------------------------------------------------------
// 1 Flowing: the groups of two, two, three, each starting low again.
constexpr Src Flow78A[]{B(0, Hold, 90), L(0.5, Hold, 2, 64), L(1.0, Hold, 0, 78, 1), T(1.5, Hold, 1, 0, 66),
                        T(2.0, Hold, 0, 0, 80), T(2.5, Hold, 2, 0, 68), T(3.0, Hold, 0, 1, 70)};
constexpr Src Flow78B[]{B(0, Hold, 90), L(0.5, Hold, 1, 64), L(1.0, Hold, 2, 78), T(1.5, Hold, 2, 0, 66),
                        T(2.0, Hold, 0, 0, 80), T(2.5, Hold, 1, 0, 68), T(3.0, Hold, 2, 0, 64)};
// 2 Pop: sixteenth figures on the 2+2+3 groups, each starting low again, the bass on the groups.
constexpr Src Pop78A[]{B(0, 0.95, 92), L(1.0, 0.95, 2, 80), B(2.0, 1.4, 86, 1),
                       T(0.00, S, 0, 0, 86), T(0.25, S, 2, 0, 62), T(0.50, S, 0, 1, 68), T(0.75, S, 2, 0, 62),
                       T(1.00, S, 1, 0, 82), T(1.25, S, 2, 0, 62), T(1.50, S, 1, 1, 68), T(1.75, S, 2, 0, 62),
                       T(2.00, S, 0, 0, 84), T(2.25, S, 2, 0, 62), T(2.50, S, 0, 1, 68),
                       T(2.75, S, 1, 1, 82), T(3.00, S, 0, 1, 64), T(3.25, S, 2, 0, 62)};
constexpr Src Pop78B[]{B(0, 0.95, 92), L(1.0, 0.95, 1, 80), L(2.0, 0.9, 2, 86), B(3.0, 0.45, 78, 1),
                       T(0.00, S, 0, 0, 86), T(0.25, S, 1, 0, 62), T(0.50, S, 2, 0, 68), T(0.75, S, 0, 1, 62),
                       T(1.00, S, 1, 0, 82), T(1.25, S, 2, 0, 62), T(1.50, S, 0, 1, 68), T(1.75, S, 1, 1, 62),
                       T(2.00, S, 0, 0, 84), T(2.25, S, 1, 0, 62), T(2.50, S, 2, 0, 68),
                       T(2.75, S, 0, 1, 82), T(3.00, S, 2, 0, 64), T(3.25, S, 1, 0, 62)};
// 3 Comping: on the groups, with pushes inside the group of three.
constexpr Src Comp78A[]{B(0, 0.9, 92), C(0, 0.9, 90), L(1.0, 0.9, 2, 82), C(1.0, 0.45, 82), B(2.0, 0.9, 86, 1),
                        C(2.0, 0.45, 84), U(2.5, 0.4, 70), U(3.0, 0.4, 72)};
constexpr Src Comp78B[]{B(0, 0.9, 92), C(0, 0.45, 90), U(0.5, 0.4, 76), L(1.0, 0.9, 1, 82), C(1.0, 0.45, 82),
                        L(2.0, 0.9, 2, 86), C(2.0, 0.9, 84), C(3.0, 0.45, 86)};
// 4 Ballad.
constexpr Src Ball78A[]{B(0, Hold, 88), Rl(0, Hold, 82, 35), L(2.0, Hold, 2, 62),
                        T(1.0, 1.0, 2, 1, 68), T(2.0, 1.0, 1, 1, 72), T(3.0, 0.5, 0, 2, 66)};
constexpr Src Ball78B[]{B(0, Hold, 86), U(0, 1.0, 68), L(1.0, 1.0, 1, 62), B(2.0, Hold, 70, 1), T(1.0, 1.0, 2, 1, 66), Rl(2.0, Hold, 72, 30),
                        T(2.5, 1.0, 1, 1, 68)};

// ---- 5/4 = 3+2 ------------------------------------------------------------------------------
// 1 Flowing: three beats and two, the bass struck again on the group of two.
constexpr Src Flow54A[]{B(0, 3.0, 90), L(0.5, Hold, 2, 64), T(1.0, Hold, 0, 0, 70), T(1.5, Hold, 1, 0, 68),
                        T(2.0, Hold, 2, 0, 72), T(2.5, Hold, 0, 1, 68), L(3.0, Hold, 0, 84, 1),
                        T(3.5, Hold, 1, 0, 68), T(4.0, Hold, 2, 0, 72), T(4.5, Hold, 0, 1, 66)};
constexpr Src Flow54B[]{B(0, 3.0, 90), L(0.5, Hold, 1, 64), T(1.0, Hold, 1, 0, 70), T(1.5, Hold, 2, 0, 68),
                        T(2.0, Hold, 0, 1, 72), T(2.5, Hold, 1, 1, 70), L(3.0, Hold, 2, 84),
                        T(3.5, Hold, 2, 0, 68), T(4.0, Hold, 0, 1, 72), T(4.5, Hold, 2, 0, 64)};
// 2 Pop: a sixteenth figure per beat, climbing through the group of three, starting low again on the group of two.
constexpr Src Pop54A[]{B(0, 1.4, 92), L(1.5, 1.4, 2, 84), B(3.0, 1.4, 86, 1), L(4.5, 0.45, 2, 76),
                       T(0.00, S, 0, 0, 86), T(0.25, S, 2, 0, 62), T(0.50, S, 0, 1, 68), T(0.75, S, 2, 0, 62),
                       T(1.00, S, 1, 0, 82), T(1.25, S, 2, 0, 62), T(1.50, S, 0, 1, 68), T(1.75, S, 1, 1, 62),
                       T(2.00, S, 2, 0, 82), T(2.25, S, 0, 1, 62), T(2.50, S, 1, 1, 68), T(2.75, S, 2, 1, 62),
                       T(3.00, S, 0, 0, 86), T(3.25, S, 2, 0, 62), T(3.50, S, 0, 1, 68), T(3.75, S, 2, 0, 62),
                       T(4.00, S, 1, 0, 82), T(4.25, S, 2, 0, 62), T(4.50, S, 0, 1, 68), T(4.75, S, 1, 1, 62)};
constexpr Src Pop54B[]{B(0, 1.4, 92), L(1.5, 1.4, 1, 84), L(3.0, 0.9, 2, 86), B(4.0, 0.45, 78, 1), L(4.5, 0.45, 2, 76),
                       T(0.00, S, 0, 0, 86), T(0.25, S, 1, 0, 62), T(0.50, S, 2, 0, 68), T(0.75, S, 0, 1, 62),
                       T(1.00, S, 1, 0, 82), T(1.25, S, 2, 0, 62), T(1.50, S, 0, 1, 68), T(1.75, S, 1, 1, 62),
                       T(2.00, S, 2, 0, 82), T(2.25, S, 0, 1, 62), T(2.50, S, 1, 1, 68), T(2.75, S, 0, 1, 62),
                       T(3.00, S, 0, 0, 86), T(3.25, S, 1, 0, 62), T(3.50, S, 2, 0, 68), T(3.75, S, 0, 1, 62),
                       T(4.00, S, 2, 0, 82), T(4.25, S, 0, 1, 62), T(4.50, S, 1, 1, 68), T(4.75, S, 0, 1, 62)};
// 3 Comping: a push in the group of three, the group of two anticipating the next bar.
constexpr Src Comp54A[]{B(0, 1.4, 92), C(0, 1.3, 92), C(1.5, 0.45, 84), U(2.5, 0.4, 70),
                        L(3.0, 1.4, 2, 86), C(3.0, 0.9, 86), U(4.5, 0.45, 78), B(4.5, 0.45, 74, 1)};
constexpr Src Comp54B[]{B(0, 0.9, 92), C(0, 0.9, 92), U(1.0, 0.4, 72), U(2.0, 0.4, 74), L(2.5, 0.4, 1, 76),
                        L(3.0, 0.9, 2, 86), C(3.0, 0.45, 86), U(3.5, 0.4, 70), C(4.5, 0.45, 86)};
// 4 Ballad.
constexpr Src Ball54A[]{B(0, 3.0, 88), Rl(0, Hold, 82, 35), T(1.0, 1.0, 2, 1, 68), T(2.0, 1.0, 1, 1, 72),
                        L(3.0, Hold, 2, 72), T(3.0, 1.0, 2, 1, 70), T(4.0, 1.0, 0, 2, 66)};
constexpr Src Ball54B[]{B(0, 3.0, 86), U(0, 2.0, 68), L(1.5, 1.5, 1, 62), T(1.5, 1.5, 1, 1, 68), L(3.0, Hold, 0, 72, 1),
                        Rl(3.0, Hold, 72, 30), T(4.0, 1.0, 2, 1, 66)};

// ---------------------------------------------------------------------------------------------

struct Written {
    double bar;                           // the meter's bar, in beats
    std::span<const Src> a, b;            // bar 1, bar 2
};

enum Family { F44, F34, F68, F78, F54, Families };

constexpr Written Table[Families][4]{
    {{4.0, Flow44A, Flow44B}, {4.0, Pop44A, Pop44B}, {4.0, Comp44A, Comp44B}, {4.0, Ball44A, Ball44B}},
    {{3.0, Flow34A, Flow34B}, {3.0, Pop34A, Pop34B}, {3.0, Comp34A, Comp34B}, {3.0, Ball34A, Ball34B}},
    {{3.0, Flow68A, Flow68B}, {3.0, Pop68A, Pop68B}, {3.0, Comp68A, Comp68B}, {3.0, Ball68A, Ball68B}},
    {{3.5, Flow78A, Flow78B}, {3.5, Pop78A, Pop78B}, {3.5, Comp78A, Comp78B}, {3.5, Ball78A, Ball78B}},
    {{5.0, Flow54A, Flow54B}, {5.0, Pop54A, Pop54B}, {5.0, Comp54A, Comp54B}, {5.0, Ball54A, Ball54B}},
};

int cleanDenominator(int denominator) noexcept
{
    for (int d : {1, 2, 4, 8, 16, 32}) if (d == denominator) return d;
    return 4;
}

// The written family a meter is played from, its bar in the family's beats, and the factor
// from those beats to the meter's (1, except sixteenth and thirty-second meters).
struct Choice {
    Family family = F44;
    double bar = 4.0;
    double scale = 1.0;
};

Choice choose(int numerator, int denominator) noexcept
{
    Choice c;
    if (denominator <= 4) {
        const int q = numerator * 4 / denominator;                     // the bar in quarter notes
        c.bar = q;
        c.family = q == 3 ? F34 : q == 5 ? F54 : q % 3 == 0 ? F34 : F44;   // 6/4 = 3+3, 7/4 = 4+3
        return c;
    }
    c.scale = 8.0 / denominator;
    c.bar = numerator * 0.5;                                           // in eighths of the family
    if (numerator % 3 == 0 || numerator == 5) c.family = F68;          // 6/8 9/8 12/8, 5/8 = 3+2
    else if (numerator % 2 == 0) c.family = F44;                       // 4/8 = 2/4, 8/8 = 3+3+2
    else c.family = F78;                                               // 7/8 and 11/8 = 2+2+3+2+2
    return c;
}

// Group starts in eighths (d >= 8) or quarters (d <= 4) of the bar.
int groupUnits(int numerator, int denominator, std::array<int, 32>& out) noexcept
{
    int n = 0;
    const auto add = [&](int u) { if (n < 32) out[std::size_t(n++)] = u; };
    const int units = denominator <= 4 ? numerator * 4 / denominator : numerator;
    if (denominator <= 4) {
        if (units == 5) { add(0); add(3); }
        else if (units == 7) { add(0); add(4); }
        else if (units % 3 == 0) for (int u = 0; u < units; u += 3) add(u);
        else if (units % 2 == 0) for (int u = 0; u < units; u += 2) add(u);
        else { for (int u = 0; u < units - 3; u += 2) add(u); add(std::max(units - 3, 0)); }
    } else {
        if (units % 3 == 0) for (int u = 0; u < units; u += 3) add(u);
        else if (units == 5) { add(0); add(3); }
        else if (units % 2 == 0) for (int u = 0; u < units; u += 4) add(u);   // as 4/4: on the half bar
        else {                                                         // 2+2+3, then twos
            add(0);
            if (units >= 7) { add(2); add(4); }
            for (int u = 7; u < units - 1; u += 2) add(u);
        }
    }
    return n;
}

bool near(double a, double b) noexcept { return std::abs(a - b) < 1e-9; }

// Weak sixteenths are played this many ms late (early if negative), and this much softer or
// louder: a fixed sequence, so every playback is the same.
constexpr std::array<double, 6> NudgeMs{3.0, -2.0, 4.0, -3.0, 2.0, -4.0};
constexpr std::array<int, 6> Jitter{-3, 2, -1, 3, -2, 1};

} // namespace

int groupStarts(int numerator, int denominator, std::array<double, 32>& out) noexcept
{
    numerator = std::clamp(numerator, 1, 32);
    denominator = cleanDenominator(denominator);
    std::array<int, 32> units{};
    const int n = groupUnits(numerator, denominator, units);
    const double unit = denominator <= 4 ? 1.0 : 4.0 / denominator;
    for (int i = 0; i < n; ++i) out[std::size_t(i)] = units[std::size_t(i)] * unit;
    return n;
}

void buildPattern(int mode, int numerator, int denominator, Part part, Pattern& out) noexcept
{
    numerator = std::clamp(numerator, 1, 32);
    denominator = cleanDenominator(denominator);
    out.count = 0;
    out.bars = PatternBars;
    out.barBeats = numerator * 4.0 / denominator;
    out.pedal = mode == 1 || mode == 4;
    if (mode < 1 || mode > 4) { out.pedal = false; return; }

    const Choice choice = choose(numerator, denominator);
    const Written& w = Table[choice.family][mode - 1];
    std::array<double, 32> groups{};
    const int groupCount = groupStarts(numerator, denominator, groups);
    const auto isGroupStart = [&](double beatInBar) {
        for (int g = 0; g < groupCount; ++g) if (near(groups[std::size_t(g)], beatInBar)) return true;
        return false;
    };
    // By the meter: the one, the group starts, the beats (quarters of x/4, eighths of x/8), the
    // off-beat eighths, the rest.
    const auto accentAt = [&](double beatInBar) {
        const auto on = [&](double unit) { return near(std::remainder(beatInBar, unit), 0); };
        if (near(beatInBar, 0)) return AccentOne;
        if (isGroupStart(beatInBar)) return AccentGroup;
        if (denominator >= 8) return on(0.5) ? AccentPulse : AccentSixteenth;
        if (on(1.0)) return AccentBeat;
        return on(0.5) ? AccentOffbeat : AccentSixteenth;
    };

    int weak = 0;                                                      // weak sixteenths so far
    for (int bar = 0; bar < PatternBars; ++bar) {
        const std::span<const Src> src = bar == 0 ? w.a : w.b;
        const double barStart = bar * out.barBeats;
        // The family's bar, cut to this meter's or repeated until it is filled.
        for (double copy = 0; copy < choice.bar - 1e-9; copy += w.bar) {
            for (const Src& s : src) {
                const double inBar = copy + s.beat;                    // family beats
                if (inBar >= choice.bar - 1e-9) continue;
                const bool leftHand = s.kind == Kind::Bass || s.kind == Kind::Low;
                if (part == Part::Bass && !leftHand) continue;
                Kind kind = s.kind;
                int degree = s.degree, octave = s.octave;
                if (part == Part::RightHand && leftHand) {
                    // Right hand only: where the left hand played alone, the right hand plays that
                    // chord tone, so the rhythm stays; where both played, the left hand's goes.
                    bool both = false;
                    for (const Src& o : src)
                        if (o.kind != Kind::Bass && o.kind != Kind::Low && near(o.beat, s.beat)) both = true;
                    if (both) continue;
                    kind = Kind::Tone;
                    degree = s.kind == Kind::Bass ? 0 : s.degree;
                    octave = 0;
                }
                if (out.count >= MaxSteps) break;
                const double beatInBar = inBar * choice.scale;
                double length = (s.length == Hold ? choice.bar - inBar : s.length) * choice.scale;
                length = std::min(length, out.barBeats - beatInBar);
                PatternStep step;
                step.beat = barStart + beatInBar;
                step.length = std::max(length, 0.05 * choice.scale);
                step.kind = kind;
                step.degree = degree;
                step.octave = octave;
                step.spreadMs = s.spreadMs;
                int velocity = s.velocity;
                if (near(beatInBar, 0)) velocity += 8;                 // the bar's one
                else if (isGroupStart(beatInBar)) velocity += 5;       // the meter's groups
                const double sixteenths = inBar * 4;
                const long whole = std::lround(sixteenths);
                if (near(sixteenths, double(whole)) && whole % 2 == 1) {
                    step.nudgeMs = NudgeMs[std::size_t(weak % 6)];
                    velocity += Jitter[std::size_t(weak % 6)];
                    ++weak;
                }
                step.velocity = std::clamp(velocity, 1, 127);
                step.accent = accentAt(beatInBar);
                out.steps[std::size_t(out.count++)] = step;
            }
        }
    }
    std::stable_sort(out.steps.begin(), out.steps.begin() + out.count,
                     [](const PatternStep& a, const PatternStep& b) { return a.beat < b.beat; });
}

std::string_view autoplayName(int mode) noexcept
{
    switch (mode) {
    case 0: return "Off";
    case 1: return "Flowing";
    case 2: return "Pop";
    case 3: return "Comping";
    case 4: return "Ballad";
    default: return "";
    }
}

std::string_view autoplayDescription(int mode) noexcept
{
    switch (mode) {
    case 0: return "Autoplay off: a strip sounds while you hold it";
    case 1: return "A broken chord in eighths over a held bass, pedalled; written for each meter (4/4 in 3+3+2, 7/8 in 2+2+3)";
    case 2: return "A syncopated sixteenth arpeggio (4/4: 3+3+3+3+2+2), bass on 1 and the and of 2";
    case 3: return "Rhythmic chords with pushes over a moving bass (3/4: oom-pah-pah, 6/8: on eighths 1 3 4 6)";
    case 4: return "A rolled chord, then a slow top line of chord tones, pedalled";
    default: return "";
    }
}

} // namespace chords
