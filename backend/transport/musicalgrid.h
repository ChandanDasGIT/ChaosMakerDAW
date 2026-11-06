#pragma once

// One musical grid for every editor (timeline and piano roll). Beats are quarter notes.
// The grid restarts at every bar line, so steps that do not divide the bar (triplets, dotted
// notes, odd meters) still land on bar lines; when the step divides the bar this is the same
// as a plain grid from beat 0. Pure C++, no Qt.
class MusicalGrid {
public:
    enum class Rounding { Nearest, Floor, Ceil };

    // beatsPerBar in quarter notes (4/4 -> 4, 6/8 -> 3); 0 means "no bars" (one grid from 0).
    // beatUnit is one counted beat (4/4 -> 1, 6/8 -> 0.5). Invalid values are ignored.
    void setMeter(double beatsPerBar, double beatUnit) noexcept;
    // Grid step in beats; 0 (or invalid) turns snapping off.
    void setStep(double stepBeats) noexcept;

    double beatsPerBar() const noexcept { return m_beatsPerBar; }
    double beatUnit() const noexcept { return m_beatUnit; }
    double step() const noexcept { return m_step; }

    // Snap a position to the grid within its bar; the next bar line counts as a grid line.
    // Ties round up. With no step the position is returned unchanged. Never below 0.
    double snap(double beat, Rounding rounding = Rounding::Nearest) const noexcept;
    // Round a drag distance to whole steps (keeps the grab point under the cursor).
    double snapDelta(double deltaBeats) const noexcept;

    // Spacing for drawn grid lines at a zoom: the step, doubled until lines are at least
    // minimumPixels apart. capAtBeat keeps at least one line per counted beat. With no step,
    // one line per counted beat.
    double lineSpacing(double pixelsPerBeat, bool capAtBeat, double minimumPixels = 6.0) const noexcept;
    // Lines per bar for that spacing (the grid restarts at each bar).
    int linesPerBar(double spacingBeats) const noexcept;

    // True when position is a whole multiple of unit, within tolerance (in units).
    static bool isOnGrid(double position, double unit, double tolerance = 1e-6) noexcept;

private:
    double m_beatsPerBar = 4.0;
    double m_beatUnit = 1.0;
    double m_step = 0.0;
};
