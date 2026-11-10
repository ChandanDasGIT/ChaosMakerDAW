#pragma once

#include <cstdint>
#include <vector>

// A track's volume envelope (restMap G6), like Reaper's. It multiplies the track fader:
// a line at 0 dB changes nothing. Points are in project samples, like clips, so they stay with
// the audio when the BPM changes.
namespace automation {

// The envelope's vertical scale, shaped like a fader: a level of 0 is silence (-inf dB) and 1 is
// +6 dB, gain = maxGain * level^3 (0 dB sits about 4/5 of the way up). The engine follows the
// same scale between points, so the line that is drawn is what is heard.
namespace envelopescale {
inline constexpr double maxDb = 6.0;
// Nonfinite levels are treated as 0; levels are clamped to 0..1.
double gainForLevel(double level) noexcept;
double dbForLevel(double level) noexcept;   // -infinity for level 0
double levelForDb(double db) noexcept;      // -inf (or below the floor) gives 0; above +6 gives 1
double unityLevel() noexcept;               // the level of 0 dB
}

struct EnvelopePoint {
    std::int64_t position = 0;   // project samples, >= 0
    double level = 0.0;          // 0..1 on envelopescale
    bool operator==(const EnvelopePoint&) const = default;
};

class VolumeEnvelope {
public:
    const std::vector<EnvelopePoint>& points() const noexcept { return m_points; }
    bool bypassed() const noexcept { return m_bypassed; }
    // View state (not undoable): the lane is shown, and drawn over the clips instead of in its
    // own lane under the track.
    bool shown() const noexcept { return m_shown; }
    bool overClips() const noexcept { return m_overClips; }
    void setBypassed(bool bypassed) noexcept { m_bypassed = bypassed; }
    void setShown(bool shown) noexcept { m_shown = shown; }
    void setOverClips(bool overClips) noexcept { m_overClips = overClips; }

    // Heard only with at least one point and not bypassed.
    bool active() const noexcept { return !m_bypassed && !m_points.empty(); }
    // The line's level at a position: the first point's level before it, the last one's after
    // it, straight lines in between. Unity when there are no points (bypass is ignored here so
    // the drawn line stays where it is when bypassed).
    double levelAt(std::int64_t position) const noexcept;
    // The gain the engine applies: 1 when not active.
    double gainAt(std::int64_t position) const noexcept;

    // A new point; its index, or -1 for a negative position, a nonfinite level or a position
    // that already holds a point. The level is clamped to 0..1.
    int addPoint(std::int64_t position, double level);
    // Move a point in time and level. It cannot pass its neighbours (it stops one sample short)
    // or go below 0; the level is clamped. False for a bad index or a nonfinite level.
    bool movePoint(int index, std::int64_t position, double level);
    bool removePoint(int index);
    // Replace every point (loading a file). False, and no change, unless positions are >= 0 and
    // strictly increasing and levels are finite and within 0..1.
    bool setPoints(std::vector<EnvelopePoint> points);
    // Positions times ratio (a project reopened at another sample rate). Points that land on
    // the same sample keep only the later one.
    void rescale(double ratio);
    // The undoable part (points and bypass) of another envelope; the view state stays.
    void restoreContent(const VolumeEnvelope& other);
    bool sameContent(const VolumeEnvelope& other) const noexcept
    {
        return m_bypassed == other.m_bypassed && m_points == other.m_points;
    }

private:
    std::vector<EnvelopePoint> m_points;   // sorted, positions strictly increasing
    bool m_bypassed = false;
    bool m_shown = false;
    bool m_overClips = false;
};

} // namespace automation
