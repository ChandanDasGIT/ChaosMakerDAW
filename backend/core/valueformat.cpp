#include "valueformat.h"

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
    const double milliseconds = std::floor(seconds * 1000.0);
    // 2^64 is the first double outside the unsigned counter range.
    if (!std::isfinite(milliseconds) || seconds < 0 || milliseconds >= std::ldexp(1.0, 64))
        return unavailable;
    const auto ms = static_cast<std::uint64_t>(milliseconds);
    return padded(ms / 3600000, 2) + ':' + padded(ms / 60000 % 60, 2)
        + ':' + padded(ms / 1000 % 60, 2) + '.' + padded(ms % 1000, 3);
}
