#include "chaoseqsettings.h"

#include <algorithm>
#include <cmath>

namespace chaoseq {
namespace {

constexpr std::array<std::pair<eqband::Shape, std::string_view>, 8> shapeNames{{
    {eqband::Shape::Bell, "bell"},           {eqband::Shape::LowShelf, "lowShelf"},
    {eqband::Shape::HighShelf, "highShelf"}, {eqband::Shape::LowCut, "lowCut"},
    {eqband::Shape::HighCut, "highCut"},     {eqband::Shape::Notch, "notch"},
    {eqband::Shape::BandPass, "bandPass"},   {eqband::Shape::TiltShelf, "tiltShelf"},
}};

constexpr std::array<std::pair<Placement, std::string_view>, 5> placementNames{{
    {Placement::Stereo, "stereo"}, {Placement::Left, "left"}, {Placement::Right, "right"},
    {Placement::Mid, "mid"},       {Placement::Side, "side"},
}};

} // namespace

int firstFreeBand(const Settings& settings) noexcept
{
    for (int i = 0; i < MaxBands; ++i)
        if (!settings.bands[static_cast<std::size_t>(i)].inUse) return i;
    return -1;
}

int bandsInUse(const Settings& settings) noexcept
{
    return static_cast<int>(std::count_if(settings.bands.begin(), settings.bands.end(),
                                          [](const Band& b) { return b.inUse; }));
}

int soloBand(const Settings& settings) noexcept
{
    for (int i = 0; i < MaxBands; ++i) {
        const auto& band = settings.bands[static_cast<std::size_t>(i)];
        if (band.inUse && !band.bypassed && band.solo) return i;
    }
    return -1;
}

Settings sanitize(const Settings& settings, double sampleRate) noexcept
{
    Settings s = settings;
    for (auto& band : s.bands) {
        if (!band.inUse) {
            band = Band{};
            continue;
        }
        band.filter = eqband::sanitize(band.filter, sampleRate);
        switch (band.placement) {
        case Placement::Stereo: case Placement::Left: case Placement::Right:
        case Placement::Mid: case Placement::Side:
            break;
        default:
            band.placement = Placement::Stereo;
        }
    }
    s.outputGainDb = std::isfinite(s.outputGainDb) ? std::clamp(s.outputGainDb, MinOutputDb, MaxOutputDb) : 0.0;
    return s;
}

double autoGainDb(const Settings& settings) noexcept
{
    constexpr double rate = 48000.0;
    constexpr int points = 96;
    std::array<eqband::Design, MaxBands> designs{};
    int count = 0;
    for (const auto& band : settings.bands) {
        if (!band.inUse || band.bypassed || !eqband::usesGain(band.filter.shape)) continue;
        designs[static_cast<std::size_t>(count++)] = eqband::design(band.filter, rate);
    }
    if (count == 0) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < points; ++i) {
        const double hz = 20.0 * std::pow(1000.0, (i + 0.5) / points);
        for (int k = 0; k < count; ++k)
            sum += eqband::magnitudeDb(designs[static_cast<std::size_t>(k)], hz, rate);
    }
    return std::clamp(-sum / points, MinOutputDb, MaxOutputDb);
}

std::string_view shapeName(eqband::Shape shape) noexcept
{
    for (const auto& [value, name] : shapeNames)
        if (value == shape) return name;
    return "bell";
}

std::string_view placementName(Placement placement) noexcept
{
    for (const auto& [value, name] : placementNames)
        if (value == placement) return name;
    return "stereo";
}

bool shapeFromName(std::string_view name, eqband::Shape& shape) noexcept
{
    for (const auto& [value, text] : shapeNames)
        if (text == name) { shape = value; return true; }
    return false;
}

bool placementFromName(std::string_view name, Placement& placement) noexcept
{
    for (const auto& [value, text] : placementNames)
        if (text == name) { placement = value; return true; }
    return false;
}

bool slopeFromDbPerOctave(int dbPerOctave, eqband::Slope& slope) noexcept
{
    for (auto candidate : {eqband::Slope::Db6, eqband::Slope::Db12, eqband::Slope::Db24, eqband::Slope::Db48})
        if (eqband::dbPerOctave(candidate) == dbPerOctave) { slope = candidate; return true; }
    return false;
}

} // namespace chaoseq
