#pragma once

#include <optional>
#include <string>
#include <string_view>

// Tempo entry rules and edit state. No Qt or audio-thread work.
class TempoEditor {
public:
    static std::optional<double> parse(std::u16string_view text);

    double value() const { return m_value; }
    const std::u16string& text() const { return m_text; }
    bool editing() const { return m_editing; }
    bool acceptableInput() const { return parse(m_text).has_value(); }

    void setValue(double value);
    void setText(std::u16string text);
    void setEditing(bool editing) { m_editing = editing; }
    std::optional<double> commit() const;
    std::optional<double> adjust(int steps) const;
    void restoreText();

private:
    double m_value = 120.0;
    std::u16string m_text = u"120";
    bool m_editing = false;
};
