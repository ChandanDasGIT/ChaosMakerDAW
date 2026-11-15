#pragma once

#include <string>

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

    // Drawing a long run of equally spaced lines or labels (unitPixels apart) in a scrolled view:
    // draw every stride-th one so they stay at least minimumPixels apart, starting at the first
    // visible index, and draw visibleLineCount of them (plus padding off-screen). Invalid sizes
    // give stride 1 and nothing to draw.
    static int lineStride(double unitPixels, double minimumPixels) noexcept;
    static double firstVisibleLine(double scrollPixels, double unitPixels, int stride) noexcept;
    static int visibleLineCount(double viewPixels, double unitPixels, int stride, int padding = 3) noexcept;

    // Beat ticks (one per counted beat, e.g. a ruler) and per-bar grid lines over a length.
    // All inputs are arguments so a view can bind to them. Beats are quarter notes.
    static int countedBeatsPerBar(double beatsPerBar, double beatUnit) noexcept;   // 4/4 -> 4, 6/8 -> 6
    static int tickCount(double lengthBeats, double beatUnit, bool includeEnd) noexcept;
    static bool isBarTick(int tickIndex, int countedBeatsPerBar) noexcept;
    // "1" on a bar's first beat, otherwise "bar.beat" (both 1-based), e.g. "2.3".
    static std::string tickLabel(int tickIndex, int countedBeatsPerBar);
    static int lineCount(double lengthBeats, double beatsPerBar, int linesPerBar) noexcept;
    static double lineOffsetInBar(int lineIndex, int linesPerBar, double spacingBeats) noexcept;
    static double linePosition(int lineIndex, int linesPerBar, double spacingBeats, double beatsPerBar) noexcept;
    static bool isBarLineIndex(int lineIndex, int linesPerBar) noexcept;

    // True when position is a whole multiple of unit, within tolerance (in units).
    static bool isOnGrid(double position, double unit, double tolerance = 1e-6) noexcept;

private:
    double m_beatsPerBar = 4.0;
    double m_beatUnit = 1.0;
    double m_step = 0.0;
};
