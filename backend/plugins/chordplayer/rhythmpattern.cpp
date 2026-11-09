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

// Pop, Comping and Ballad's first variations, written per meter family (Flowing and the other
// variations are built from the meter: see generated() below).
constexpr Written Table[Families][3]{
    {{4.0, Pop44A, Pop44B}, {4.0, Comp44A, Comp44B}, {4.0, Ball44A, Ball44B}},
    {{3.0, Pop34A, Pop34B}, {3.0, Comp34A, Comp34B}, {3.0, Ball34A, Ball34B}},
    {{3.0, Pop68A, Pop68B}, {3.0, Comp68A, Comp68B}, {3.0, Ball68A, Ball68B}},
    {{3.5, Pop78A, Pop78B}, {3.5, Comp78A, Comp78B}, {3.5, Ball78A, Ball78B}},
    {{5.0, Pop54A, Pop54B}, {5.0, Comp54A, Comp54B}, {5.0, Ball54A, Ball54B}},
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

namespace {

// ---------------------------------------------------------------------------------------------
// Variations built from the meter. The right hand plays figures on a spread chord, the way a
// pianist does: rungs of root (1), third (3), fifth (5), octave (8), tenth (10), twelfth (12),
// fifteenth (15) and seventeenth (17), counted from the zone's root. The bass is struck with the
// first note of each figure, on the beat, and rings under it; bar 2 always differs from bar 1.

struct Rung { int degree, octave; };
constexpr Rung N1{0, 0}, N3{1, 0}, N5{2, 0}, N8{0, 1}, N10{1, 1}, N12{2, 1}, N15{0, 2}, N17{1, 2};

constexpr int MaxSrc = 128;
struct SrcBar {
    std::array<Src, MaxSrc> steps{};
    int count = 0;
    void add(const Src& s) noexcept { if (count < MaxSrc) steps[std::size_t(count++)] = s; }
    std::span<const Src> span() const noexcept { return {steps.data(), std::size_t(count)}; }
};

constexpr Src tone(double beat, double length, Rung r, int velocity) noexcept
{
    return T(beat, length, r.degree, r.octave, velocity);
}

// The bass notes by code: 0 root, 1 third, 2 fifth, 3 octave.
constexpr Src bassNote(double beat, double length, int code, int velocity) noexcept
{
    return code == 1 ? L(beat, length, 1, velocity)
         : code == 2 ? L(beat, length, 2, velocity)
         : code == 3 ? B(beat, length, velocity, 1)
                     : B(beat, length, velocity);
}

// Up a list of rungs and back down, bouncing between its second and its top rung, `count` notes:
// the arpeggio's contour over a stretch of the bar (`top` rungs used going up).
template <std::size_t N>
Rung upAndBack(const Rung (&list)[N], int top, int k) noexcept
{
    top = std::clamp(top, 2, int(N));
    if (k < top) return list[std::size_t(k)];
    const int span = top - 2;                              // between index 1 and top-1
    if (span <= 0) return list[1];
    const int phase = (k - top) % (2 * span);
    return list[std::size_t(phase < span ? top - 2 - phase : 2 + (phase - span))];
}

template <std::size_t N>
Rung cycle(const Rung (&list)[N], int k) noexcept { return list[std::size_t(k) % N]; }

void generated(int mode, int variation, int bar, double barBeats, int denominator,
               std::span<const double> groups, SrcBar& out) noexcept
{
    const bool eighths = denominator >= 8;
    const double pulse = eighths ? 0.5 : 1.0;
    const int groupCount = int(groups.size());
    const auto isGroup = [&](double t) {
        for (double g : groups) if (near(g, t)) return true;
        return false;
    };
    // The middle of the bar: the group start nearest it, or the half bar on the eighth grid.
    double middle = std::floor(barBeats / 2 / 0.5) * 0.5;
    if (groupCount >= 2) {
        double best = 1e9;
        for (double g : groups)
            if (g > 0 && std::abs(g - barBeats / 2) < best) { best = std::abs(g - barBeats / 2); middle = g; }
    }
    const double lastPulse = std::floor((barBeats - 1e-9) / pulse) * pulse;
    // Repeating figures start again on the bar's two halves (the bass marks every group).
    const auto halfOf = [&](double t) { return t >= middle - 1e-9 && middle > 0 ? middle : 0.0; };

    // ---- the bass ------------------------------------------------------------------------
    std::array<double, 40> slots{};
    std::array<int, 40> codes{};
    int slotCount = 0;
    const auto slot = [&](double t, int code) {
        if (slotCount < 40 && t >= 0 && t < barBeats - 1e-9 && (slotCount == 0 || t > slots[std::size_t(slotCount - 1)] + 0.2)) {
            slots[std::size_t(slotCount)] = t;
            codes[std::size_t(slotCount++)] = code;
        }
    };
    if (mode == 1) {
        // Flowing: struck on every group (a single-group bar also on its last beat), held;
        // bar 1 root - octave - fifth, bar 2 root - fifth - octave, and a third leading back.
        constexpr int One[]{0, 3, 2, 3}, Two[]{0, 2, 3, 2};
        if (groupCount >= 2) {
            for (int g = 0; g < groupCount; ++g) slot(groups[std::size_t(g)], (bar == 0 ? One : Two)[std::size_t(g % 4)]);
        } else {
            slot(0, 0);
            slot(lastPulse, bar == 0 ? 3 : 2);
        }
        if (bar == 1) slot(barBeats - 0.5, 1);
    } else if (mode == 2) {
        // Pop: the root on the one, a push half a beat before the middle; bar 2 walks back.
        slot(0, 0);
        slot(middle - 0.5, bar == 0 ? 2 : 1);
        if (bar == 1) { slot(barBeats - 1.0, 3); slot(barBeats - 0.5, 2); }
    } else {
        // Comping and Ballad: on the groups (a single-group bar also on its last beat), bar 1
        // root - fifth - octave, bar 2 root - third - fifth and a pickup octave. Off-beat comping
        // walks on every beat.
        constexpr int One[]{0, 2, 3, 2, 3, 2, 3, 2}, Two[]{0, 1, 2, 3, 2, 1, 2, 3};
        int i = 0;
        if (mode == 3 && variation == 2 && !eighths) {
            for (double t = 0; t < barBeats - 1e-9; t += 1.0, ++i) slot(t, (bar == 0 ? One : Two)[std::size_t(i % 8)]);
        } else {
            for (double g : groups) slot(g, (bar == 0 ? One : Two)[std::size_t(i++ % 8)]);
            if (groupCount < 2) slot(lastPulse, (bar == 0 ? One : Two)[std::size_t(i++ % 8)]);
        }
        if (bar == 1) slot(barBeats - 0.5, 3);
    }
    for (int i = 0; i < slotCount; ++i) {
        const double t = slots[std::size_t(i)];
        const double next = i + 1 < slotCount ? slots[std::size_t(i + 1)] : barBeats;
        const bool held = mode == 1 || mode == 4;
        out.add(bassNote(t, held ? next - t : std::max((next - t) * 0.9, 0.2), codes[std::size_t(i)], i == 0 ? 92 : 82));
    }

    // ---- the right hand ------------------------------------------------------------------
    const int eighthCount = int(std::lround(barBeats / 0.5));
    if (mode == 1) {
        // Flowing: eighths, each ringing under the pedal. Long bars (over ten eighths) are two
        // figures, split at the middle group.
        const int half = eighthCount > 10 ? int(std::lround(middle / 0.5)) : eighthCount;
        for (int e = 0; e < eighthCount; ++e) {
            const double t = e * 0.5;
            const int stretch = e < half ? half : eighthCount - half;
            const int k = e < half ? e : e - half;
            Rung r = N1;
            if (variation == 0) {                              // Rolling: 1-5-8-10-12-10-8-5
                static constexpr Rung Up1[]{N1, N5, N8, N10, N12, N15};
                static constexpr Rung Up2[]{N1, N8, N10, N12, N15, N17};
                r = bar == 0 ? upAndBack(Up1, stretch / 2 + 1, k) : upAndBack(Up2, std::min(stretch / 2 + 1, 5), k);
            } else if (variation == 1) {                       // Alberti: 1-5-3-5, bar 2 turns to 3-8-5-8
                static constexpr Rung A[]{N1, N5, N3, N5};
                static constexpr Rung B2[]{N3, N8, N5, N8};
                const int n = int(std::lround((t - halfOf(t)) / 0.5));
                r = bar == 1 && t >= middle - 1e-9 && middle > 0 ? cycle(B2, n) : cycle(A, n);
            } else if (variation == 2) {                       // Open: 1-5-10-5, bar 2 1-5-12-10
                static constexpr Rung O1[]{N1, N5, N10, N5};
                static constexpr Rung O2[]{N1, N5, N12, N10};
                const int n = int(std::lround((t - halfOf(t)) / 0.5));
                r = bar == 0 ? cycle(O1, n) : cycle(O2, n);
            } else {                                           // Cascade: from the top down, then back up
                static constexpr Rung D1[]{N12, N10, N8, N5, N3};
                static constexpr Rung D2[]{N15, N12, N10, N8, N5};
                r = upAndBack(bar == 0 ? D1 : D2, stretch / 2 + 1, k);
            }
            out.add(tone(t, Hold, r, e == 0 ? 80 : 70));
        }
        return;
    }
    if (mode == 2) {
        const int sixteenths = int(std::lround(barBeats / 0.25));
        if (variation == 1) {                                  // Pulse: driving eighth chords
            for (int e = 0; e < eighthCount; ++e) {
                const double t = e * 0.5;
                if (bar == 1 && e == eighthCount - 1) { out.add(U(t + 0.25, 0.2, 80)); continue; }   // a push into the loop
                out.add(isGroup(t) ? C(t, 0.45, 90) : U(t, 0.4, 76));
            }
            return;
        }
        static constexpr Rung Os1[]{N8, N10, N5, N10};        // Ostinato: a high riff
        static constexpr Rung Os2[]{N8, N12, N10, N12};
        static constexpr Rung Sh1[]{N1, N8, N5, N8, N3, N8, N5, N8};       // Shimmer: under a held-high top
        static constexpr Rung Sh2[]{N1, N10, N5, N10, N8, N10, N5, N10};
        for (int x = 0; x < sixteenths; ++x) {
            const double t = x * 0.25;
            const int n = int(std::lround((t - halfOf(t)) / 0.25));
            const Rung r = variation == 2 ? (bar == 0 ? cycle(Os1, n) : cycle(Os2, n))
                                          : (bar == 0 ? cycle(Sh1, n) : cycle(Sh2, n));
            out.add(tone(t, S, r, x % 4 == 0 ? 84 : 66));
        }
        return;
    }
    const auto groupEnd = [&](double g) {
        double end = barBeats;
        for (double o : groups) if (o > g + 1e-9) end = std::min(end, o);
        return end;
    };
    if (mode == 3 && variation == 1) {                         // Steady: on every beat, bar 2 pushes
        for (double t = 0; t < barBeats - 1e-9; t += pulse) {
            const bool last = t + pulse >= barBeats - 1e-9;
            if (bar == 1 && last && t > 0) {                   // the last beat pushed ahead of it
                out.add(U(eighths ? barBeats - 0.25 : barBeats - 0.5, eighths ? 0.2 : 0.45, 82));
                continue;
            }
            out.add(isGroup(t) ? C(t, pulse * 0.9, 88) : U(t, pulse * 0.8, 76));
        }
    } else if (mode == 3 && variation == 2) {                  // Off-beat: between the beats
        if (eighths) {
            for (double t = 0; t < barBeats - 1e-9; t += 0.5) if (!isGroup(t)) out.add(U(t, bar == 1 ? 0.2 : 0.4, 80));
            if (bar == 1) out.add(U(barBeats - 0.25, 0.2, 74));   // short stabs, a double one into the loop
        } else {
            for (double t = 0.5; t < barBeats - 1e-9; t += 1.0) out.add(U(t, 0.4, 82));
            if (bar == 1 && barBeats >= 2) out.add(U(barBeats - 0.25, 0.2, 74));   // a double stab into the loop
        }
    } else if (mode == 3) {                                    // Sparse: the one and a push
        out.add(C(0, eighths ? 1.0 : 1.4, 90));
        const double push = bar == 0 ? (eighths ? 1.0 : 1.5) : barBeats - 0.5;
        if (push > 0.4 && push < barBeats - 1e-9) out.add(U(push, 0.45, 80));
    } else if (variation == 1) {                               // Ballad, Arpeggio: up, then an answer down
        static constexpr Rung A1[]{N1, N5, N10, N12, N10, N5};
        static constexpr Rung A2[]{N8, N12, N10, N5, N10, N12};
        int k = 0;
        for (double t = 0; t < barBeats - 1e-9; t += pulse, ++k)
            out.add(tone(t, Hold, bar == 0 ? cycle(A1, k) : cycle(A2, k), k == 0 ? 78 : 70));
    } else if (variation == 2) {                               // Ballad, Hymn
        if (bar == 0 && !eighths) {                            // x/4: a chord on every beat
            for (double t = 0; t < barBeats - 1e-9; t += 1.0)
                out.add(C(t, (std::min(t + 1.0, barBeats) - t) * 0.95, isGroup(t) ? 86 : 74));
        } else if (bar == 0) {                                 // x/8: on each group, and its last eighth
            for (double g : groups) {
                const double end = groupEnd(g);
                out.add(C(g, (end - g - 0.5) * 0.95, 86));
                if (end - g > 0.75) out.add(U(end - 0.5, 0.45, 72));
            }
        } else {                                               // long chords on the groups, a short one last
            for (double g : groups) out.add(C(g, (groupEnd(g) - g) * 0.95, 84));
            if (!isGroup(lastPulse) && lastPulse > 0) out.add(U(lastPulse, pulse * 0.9, 72));
        }
    } else {                                                   // Ballad, Echo: a roll, then an answer
        static constexpr Rung E1[]{N10, N12};
        static constexpr Rung E2[]{N12, N15};
        int k = 0;
        for (double g : groups) {
            const double end = groupEnd(g);
            out.add(Rl(g, end - g, 80, 30));
            const double answer = g + std::max(pulse, std::floor((end - g) / 2 / pulse) * pulse);
            if (answer < end - 1e-9) out.add(tone(answer, end - answer, bar == 0 ? cycle(E1, k) : cycle(E2, k), 70));
            ++k;
        }
    }
}

} // namespace

void buildPattern(int mode, int variation, int numerator, int denominator, Part part, Pattern& out) noexcept
{
    numerator = std::clamp(numerator, 1, 32);
    denominator = cleanDenominator(denominator);
    variation = std::clamp(variation, 0, Variations - 1);
    out.count = 0;
    out.bars = PatternBars;
    out.barBeats = numerator * 4.0 / denominator;
    out.pedal = mode == 1 || mode == 4;
    if (mode < 1 || mode > 4) { out.pedal = false; return; }

    const Choice choice = choose(numerator, denominator);
    const Written& w = Table[choice.family][mode >= 2 ? mode - 2 : 0];
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
    const bool written = variation == 0 && mode != 1;            // Flowing is always built

    int weak = 0;                                                      // weak sixteenths so far
    SrcBar made;
    for (int bar = 0; bar < PatternBars; ++bar) {
        std::span<const Src> src = bar == 0 ? w.a : w.b;
        double familyBar = choice.bar, copyStep = w.bar, scale = choice.scale;
        if (!written) {
            made.count = 0;
            generated(mode, variation, bar, out.barBeats, denominator,
                      std::span<const double>(groups.data(), std::size_t(groupCount)), made);
            src = made.span();
            familyBar = copyStep = out.barBeats;
            scale = 1.0;
        }
        const double barStart = bar * out.barBeats;
        // The family's bar, cut to this meter's or repeated until it is filled.
        for (double copy = 0; copy < familyBar - 1e-9; copy += copyStep) {
            for (const Src& s : src) {
                const double inBar = copy + s.beat;                    // family beats
                if (inBar >= familyBar - 1e-9) continue;
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
                const double beatInBar = inBar * scale;
                double length = (s.length == Hold ? familyBar - inBar : s.length) * scale;
                length = std::min(length, out.barBeats - beatInBar);
                PatternStep step;
                step.beat = barStart + beatInBar;
                step.length = std::max(length, 0.05 * scale);
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

std::string_view variationName(int mode, int variation) noexcept
{
    static constexpr std::string_view Names[4][Variations]{
        {"Rolling", "Alberti", "Open", "Cascade"},
        {"Cells", "Pulse", "Ostinato", "Shimmer"},
        {"Pushes", "Steady", "Off-beat", "Sparse"},
        {"Top line", "Arpeggio", "Hymn", "Echo"},
    };
    if (mode < 1 || mode > 4 || variation < 0 || variation >= Variations) return "";
    return Names[mode - 1][variation];
}

std::string_view variationDescription(int mode, int variation) noexcept
{
    static constexpr std::string_view Text[4][Variations]{
        {"The classic piano arpeggio in eighths: root, fifth, octave, tenth, twelfth and back down, over the bass, pedalled",
         "The Alberti bass in eighths: root, fifth, third, fifth; bar 2 turns to third, octave, fifth, octave",
         "Wide and airy: root, fifth, tenth, fifth; bar 2 reaches the twelfth",
         "From the top of the chord down, then turning back up, over the bass, pedalled"},
        {"A syncopated sixteenth arpeggio in cells, bass on the one and the push",
         "Driving eighth-note chords, fuller on the groups, with a push into the next bar",
         "A high broken riff in sixteenths: octave, tenth, fifth, tenth",
         "Sixteenths moving under a repeated high octave, like a guitar's open string"},
        {"Chords on the one with pushes ahead of the beat, over a moving bass",
         "A chord on every beat (every eighth in x/8), fuller on the groups",
         "Chords between the beats over a walking bass, reggae and ska style",
         "Just the one and a push: a full chord, then a short stab"},
        {"A rolled chord, then a slow top line of chord tones, pedalled",
         "A slow spread arpeggio, one note per beat: up in bar 1, an answer coming down in bar 2",
         "Full chords like a hymn: one on every beat, then long chords on the groups",
         "A rolled chord on each group, answered by a high note; higher answers in bar 2"},
    };
    if (mode < 1 || mode > 4 || variation < 0 || variation >= Variations) return "";
    return Text[mode - 1][variation];
}

} // namespace chords
