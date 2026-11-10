#include "volumeenvelope.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace automation {

namespace envelopescale {
namespace {
// 10^(maxDb / 20) as a constant: the engine asks for a gain every sample.
constexpr double maxGainValue = 1.9952623149688795;
static_assert(maxDb == 6.0, "maxGainValue is 10^(6 / 20)");
double maxGain() noexcept { return maxGainValue; }
double clampLevel(double level) noexcept
{
    return std::isfinite(level) ? std::clamp(level, 0.0, 1.0) : 0.0;
}
}

double gainForLevel(double level) noexcept
{
    const double l = clampLevel(level);
    return maxGain() * l * l * l;
}

double dbForLevel(double level) noexcept
{
    const double gain = gainForLevel(level);
    return gain > 0.0 ? 20.0 * std::log10(gain) : -std::numeric_limits<double>::infinity();
}

double levelForDb(double db) noexcept
{
    if (std::isnan(db)) return 0.0;
    if (db >= maxDb) return 1.0;
    const double gain = std::pow(10.0, db / 20.0);   // -inf dB gives 0
    return std::clamp(std::cbrt(gain / maxGain()), 0.0, 1.0);
}

double unityLevel() noexcept { return std::cbrt(1.0 / maxGain()); }
} // namespace envelopescale

double curveProgress(CurveShape shape, double t) noexcept
{
    t = std::isfinite(t) ? std::clamp(t, 0.0, 1.0) : 0.0;
    switch (shape) {
    case CurveShape::Linear: return t;
    case CurveShape::Smooth: return t * t * (3.0 - 2.0 * t);
    case CurveShape::FastStart: { const double u = 1.0 - t; return 1.0 - u * u * u; }
    case CurveShape::FastEnd: return t * t * t;
    case CurveShape::Hold: return t >= 1.0 ? 1.0 : 0.0;
    }
    return t;
}

namespace {
constexpr std::string_view shapeIds[curveShapeCount] = {"linear", "smooth", "fastStart", "fastEnd", "hold"};
bool knownShape(CurveShape shape) noexcept
{
    const int value = static_cast<int>(shape);
    return value >= 0 && value < curveShapeCount;
}
}

std::string_view curveShapeId(CurveShape shape) noexcept
{
    return knownShape(shape) ? shapeIds[static_cast<int>(shape)] : shapeIds[0];
}

std::optional<CurveShape> curveShapeFromId(std::string_view id) noexcept
{
    for (int i = 0; i < curveShapeCount; ++i)
        if (shapeIds[i] == id) return static_cast<CurveShape>(i);
    return std::nullopt;
}

double VolumeEnvelope::levelAt(std::int64_t position) const noexcept
{
    if (m_points.empty()) return envelopescale::unityLevel();
    if (position <= m_points.front().position) return m_points.front().level;
    if (position >= m_points.back().position) return m_points.back().level;
    const auto next = std::upper_bound(m_points.begin(), m_points.end(), position,
        [](std::int64_t p, const EnvelopePoint& point) { return p < point.position; });
    const auto& b = *next;
    const auto& a = *(next - 1);
    const double t = double(position - a.position) / double(b.position - a.position);
    return a.level + (b.level - a.level) * curveProgress(a.shape, t);
}

double VolumeEnvelope::gainAt(std::int64_t position) const noexcept
{
    return active() ? envelopescale::gainForLevel(levelAt(position)) : 1.0;
}

int VolumeEnvelope::addPoint(std::int64_t position, double level)
{
    if (position < 0 || !std::isfinite(level)) return -1;
    const auto at = std::lower_bound(m_points.begin(), m_points.end(), position,
        [](const EnvelopePoint& point, std::int64_t p) { return point.position < p; });
    if (at != m_points.end() && at->position == position) return -1;
    const auto inserted = m_points.insert(at, {position, std::clamp(level, 0.0, 1.0)});
    return static_cast<int>(inserted - m_points.begin());
}

bool VolumeEnvelope::movePoint(int index, std::int64_t position, double level)
{
    if (index < 0 || index >= static_cast<int>(m_points.size()) || !std::isfinite(level)) return false;
    const auto i = static_cast<std::size_t>(index);
    std::int64_t low = 0, high = std::numeric_limits<std::int64_t>::max();
    if (i > 0) low = m_points[i - 1].position + 1;
    if (i + 1 < m_points.size()) high = m_points[i + 1].position - 1;
    m_points[i].position = std::clamp(position, low, std::max(low, high));
    m_points[i].level = std::clamp(level, 0.0, 1.0);
    return true;
}

bool VolumeEnvelope::removePoint(int index)
{
    if (index < 0 || index >= static_cast<int>(m_points.size())) return false;
    m_points.erase(m_points.begin() + index);
    return true;
}

bool VolumeEnvelope::setPointShape(int index, CurveShape shape)
{
    if (index < 0 || index >= static_cast<int>(m_points.size()) || !knownShape(shape)) return false;
    m_points[static_cast<std::size_t>(index)].shape = shape;
    return true;
}

bool VolumeEnvelope::setPoints(std::vector<EnvelopePoint> points)
{
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& p = points[i];
        if (p.position < 0 || !std::isfinite(p.level) || p.level < 0.0 || p.level > 1.0 || !knownShape(p.shape)
            || (i > 0 && p.position <= points[i - 1].position))
            return false;
    }
    m_points = std::move(points);
    return true;
}

void VolumeEnvelope::rescale(double ratio)
{
    if (!std::isfinite(ratio) || ratio <= 0.0 || ratio == 1.0) return;
    std::vector<EnvelopePoint> scaled;
    scaled.reserve(m_points.size());
    for (const auto& p : m_points) {
        const EnvelopePoint moved{std::llround(double(p.position) * ratio), p.level, p.shape};
        if (!scaled.empty() && scaled.back().position >= moved.position) scaled.back() = moved;
        else scaled.push_back(moved);
    }
    m_points = std::move(scaled);
}

void VolumeEnvelope::restoreContent(const VolumeEnvelope& other)
{
    m_points = other.m_points;
    m_bypassed = other.m_bypassed;
}

} // namespace automation
