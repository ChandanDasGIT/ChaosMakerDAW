#pragma once

#include <string>
#include <vector>

// Piano roll grid and snapping rules. Beats are quarter notes; positions are clip-relative.
// The grid restarts at every bar line, so triplet/dotted steps that do not divide the bar still
// line up with bars. Pure C++: the piano roll view only draws what this computes.
class PianoRollGrid {
public:
    enum class Division { Quarter, Eighth, Sixteenth, ThirtySecond, SixtyFourth, Off };
    enum class Style { Straight, Triplet, Dotted };
    enum class NoteLength { Grid, Sixteenth, Eighth, Quarter };

    // Names shown in the piano roll's Grid / style / Notes boxes, in menu order.
    static const std::vector<std::string>& divisionNames();
    static const std::vector<std::string>& styleNames();
    static const std::vector<std::string>& noteLengthNames();

    // Setters take the menu names; unknown names are rejected (returns false).
    bool setDivision(const std::string& name);
    bool setStyle(const std::string& name);
    bool setNoteLength(const std::string& name);
    void setSnapEnabled(bool enabled) noexcept { m_snapEnabled = enabled; }
    // Meter in quarter-note beats (4/4 -> 4 and 1, 6/8 -> 3 and 0.5). Invalid values are ignored.
    void setMeter(double beatsPerBar, double beatUnit) noexcept;

    std::string divisionName() const;
    std::string styleName() const;
    std::string noteLengthName() const;
    bool snapEnabled() const noexcept { return m_snapEnabled; }
    double beatsPerBar() const noexcept { return m_beatsPerBar; }
    double beatUnit() const noexcept { return m_beatUnit; }

    // The Grid division in beats including the style (triplet 2/3, dotted 1.5); 0 when Off.
    double divisionBeats() const noexcept;
    // Step used while editing: the division, or 0 when the magnet (snap) is off.
    double snapStep() const noexcept;
    // Floor a position to its grid cell (within its bar). Never below 0.
    double snapFloor(double beat) const noexcept;
    // Round a drag distance to whole grid steps (keeps the grab point under the cursor).
    double snapDelta(double deltaBeats) const noexcept;
    // Length of a newly drawn note: the Notes box choice; "Grid" = division (1/4 beat when Off).
    double newNoteLength() const noexcept;
    // Shortest length a resize can reach: the snap step, or 1/4 beat with snap off.
    double minimumNoteLength() const noexcept;
    // Step for Quantize: the division even with snap off; one meter beat when the grid is Off.
    double quantizeStep() const noexcept;

    // Grid line spacing at a zoom level: straight grids show at least one line per meter
    // beat; triplet/dotted show their own step. Doubled until lines are >= 6 px apart.
    double lineBeats(double pixelsPerBeat) const noexcept;
    // Grid lines per bar for lineBeats(pixelsPerBeat) (the grid restarts at each bar).
    int linesPerBar(double pixelsPerBeat) const noexcept;

    // True when position is a whole multiple of unit (both in beats).
    static bool isOnGrid(double position, double unit) noexcept;

private:
    Division m_division = Division::Sixteenth;
    Style m_style = Style::Straight;
    NoteLength m_noteLength = NoteLength::Grid;
    bool m_snapEnabled = true;
    double m_beatsPerBar = 4.0;
    double m_beatUnit = 1.0;
};
