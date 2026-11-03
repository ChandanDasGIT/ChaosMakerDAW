#include "tempoeditor.h"
#include "core/valueformat.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <utility>

namespace {
constexpr double minimumBpm = 0.01;
constexpr double maximumBpm = 10000.0;

bool whitespace(char16_t c)
{
    // Match the whitespace accepted by the original text entry, including pasted Unicode spaces.
    return (c >= u'\t' && c <= u'\r') || c == u' ' || c == u'\u00a0'
        || c == u'\u1680' || (c >= u'\u2000' && c <= u'\u200a')
        || c == u'\u2028' || c == u'\u2029' || c == u'\u202f'
        || c == u'\u205f' || c == u'\u3000' || c == u'\ufeff';
}
}

std::optional<double> TempoEditor::parse(std::u16string_view text)
{
    while (!text.empty() && whitespace(text.front())) text.remove_prefix(1);
    while (!text.empty() && whitespace(text.back())) text.remove_suffix(1);
    if (text.empty()) return std::nullopt;

    std::string number;
    number.reserve(text.size());
    bool separator = false;
    int integerDigits = 0;
    int fractionalDigits = 0;
    for (char16_t c : text) {
        if (c >= u'0' && c <= u'9') {
            if (separator) {
                if (++fractionalDigits > 2) return std::nullopt;
            } else {
                ++integerDigits;
            }
            number.push_back(static_cast<char>(c));
        } else if ((c == u'.' || c == u',') && !separator && integerDigits > 0) {
            separator = true;
            number.push_back('.');
        } else {
            return std::nullopt;
        }
    }

    double result = 0;
    const auto parsed = std::from_chars(number.data(), number.data() + number.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size()
        || !std::isfinite(result) || result < minimumBpm || result > maximumBpm)
        return std::nullopt;
    return result;
}

void TempoEditor::setValue(double value)
{
    if (!std::isfinite(value) || value < minimumBpm || value > maximumBpm) return;
    m_value = value;
    if (!m_editing) restoreText();
}

void TempoEditor::setText(std::u16string text)
{
    m_text = std::move(text);
}

std::optional<double> TempoEditor::commit() const
{
    const auto candidate = parse(m_text);
    return candidate && *candidate != m_value ? candidate : std::nullopt;
}

std::optional<double> TempoEditor::adjust(int steps) const
{
    const double candidate = std::clamp(
        std::round((parse(m_text).value_or(m_value) + steps) * 100.0) / 100.0,
        minimumBpm, maximumBpm);
    return candidate != m_value ? std::optional<double>(candidate) : std::nullopt;
}

void TempoEditor::restoreText()
{
    const auto formatted = valueformat::number(m_value);
    m_text.assign(formatted.begin(), formatted.end());
}
