#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
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

// The shape of the curve from a point to the next one (restMap G6b), in level (0..1) space, so
// the drawn curve is what is heard. The last point's shape has no segment to act on.
enum class CurveShape {
    Linear,      // a straight line
    Smooth,      // S-curve: eases out of the point and into the next
    FastStart,   // moves quickly first, then settles into the next point
    FastEnd,     // moves slowly first, then quickly into the next point
    Hold,        // stays at the point's level, then jumps at the next point
};
inline constexpr int curveShapeCount = 5;
// How far along the segment's level change the curve is at t (0..1 of the segment's time):
// 0 at t = 0 and 1 at t = 1 for every shape except Hold (0 until the next point).
double curveProgress(CurveShape shape, double t) noexcept;
// The ids saved in project files: "linear", "smooth", "fastStart", "fastEnd", "hold".
std::string_view curveShapeId(CurveShape shape) noexcept;
std::optional<CurveShape> curveShapeFromId(std::string_view id) noexcept;

struct EnvelopePoint {
    std::int64_t position = 0;   // project samples, >= 0
    double level = 0.0;          // 0..1 on envelopescale
    CurveShape shape = CurveShape::Linear;   // towards the next point
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
    // it, each segment following its first point's curve shape in between. Unity when there are no points (bypass is ignored here so
    // the drawn line stays where it is when bypassed).
    double levelAt(std::int64_t position) const noexcept;
    // The gain the engine applies: 1 when not active.
    double gainAt(std::int64_t position) const noexcept;

    // A new point (Linear); its index, or -1 for a negative position, a nonfinite level or a
    // position that already holds a point. The level is clamped to 0..1.
    int addPoint(std::int64_t position, double level);
    // Move a point in time and level. It cannot pass its neighbours (it stops one sample short)
    // or go below 0; the level is clamped. False for a bad index or a nonfinite level.
    bool movePoint(int index, std::int64_t position, double level);
    bool removePoint(int index);
    // The curve from a point to the next one. False for a bad index or an unknown shape.
    bool setPointShape(int index, CurveShape shape);
    // Replace every point (loading a file). False, and no change, unless positions are >= 0 and
    // strictly increasing, levels are finite and within 0..1 and shapes are known.
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
