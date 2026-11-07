#include "valueformat.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>

namespace {
constexpr const char* unavailable = "\xe2\x80\x94";
constexpr const char* negativeInfinity = "\xe2\x88\x92\xe2\x88\x9e";

std::string padded(std::uint64_t value, std::size_t width)
{
    auto text = std::to_string(value);
    if (text.size() < width) text.insert(0, width - text.size(), '0');
    return text;
}
}

std::string valueformat::number(double value)
{
    if (!std::isfinite(value)) return unavailable;
    if (value == 0) value = 0; // Display negative zero as zero.
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return result.ec == std::errc{} ? std::string(buffer, result.ptr) : unavailable;
}

std::string valueformat::fixed(double value, int decimals)
{
    if (!std::isfinite(value) || decimals < 0 || decimals > 6) return unavailable;
    // Round decimal ties away from zero as the existing QML toFixed displays did.
    // Wider intermediates preserve cases such as double(2.55), which is below 2.55.
    long double scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10;
    if (value != 0)
        value = static_cast<double>(std::round(static_cast<long double>(value) * scale) / scale);
    else
        value = 0;
    char buffer[384]; // Enough for a finite double in fixed notation plus six decimal places.
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                      std::chars_format::fixed, decimals);
    return result.ec == std::errc{} ? std::string(buffer, result.ptr) : unavailable;
}

std::string valueformat::withUnit(double value, int decimals, std::string_view unit)
{
    auto text = fixed(value, decimals);
    if (text != unavailable && !unit.empty()) {
        text += ' ';
        text += unit;
    }
    return text;
}

std::string valueformat::meterLevel(double db)
{
    if (db == -INFINITY || (std::isfinite(db) && db <= -90)) return negativeInfinity;
    return fixed(db, 1);
}

std::string valueformat::position(double seconds)
{
    // The tiny tolerance keeps exact hundredths (0.29 s is 28.999... * 0.01) from flooring down.
    const double hundredths = std::floor(seconds * 100.0 + 1e-6);
    // 2^64 is the first double outside the unsigned counter range.
    if (!std::isfinite(hundredths) || seconds < 0 || hundredths >= std::ldexp(1.0, 64))
        return unavailable;
    const auto cs = static_cast<std::uint64_t>(hundredths);
    return padded(cs / 360000, 2) + ':' + padded(cs / 6000 % 60, 2)
        + ':' + padded(cs / 100 % 60, 2) + '.' + padded(cs % 100, 2);
}

std::string valueformat::barBeatPosition(double beat, double beatsPerBar, double beatUnit)
{
    if (!std::isfinite(beat) || !std::isfinite(beatsPerBar) || !std::isfinite(beatUnit)
        || beat < 0 || beatsPerBar <= 0 || beatUnit <= 0)
        return unavailable;
    // A small tolerance keeps exact grid positions (e.g. 2.9999999) on the right beat.
    constexpr double tolerance = 1e-9;
    const double bar = std::floor(beat / beatsPerBar + tolerance);
    if (bar >= 1e15) return unavailable;
    const double inBar = std::max(0.0, beat - bar * beatsPerBar);
    const double beatInBar = std::floor(inBar / beatUnit + tolerance);
    const double fraction = std::max(0.0, inBar / beatUnit - beatInBar);
    const auto hundredths = std::min<std::uint64_t>(99, static_cast<std::uint64_t>(std::floor(fraction * 100 + 1e-6)));
    return std::to_string(static_cast<std::uint64_t>(bar) + 1) + '.'
        + std::to_string(static_cast<std::uint64_t>(beatInBar) + 1) + '.' + padded(hundredths, 2);
}
