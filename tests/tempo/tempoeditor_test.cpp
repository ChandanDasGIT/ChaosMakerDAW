#include "testkit.h"
#include "tempo/tempoeditor.h"

#include <limits>

TEST(tempoeditor, accepts_decimal_separators_whitespace_and_range_boundaries)
{
    for (const auto& [text, expected] : {
             std::pair{u"137.5", 137.5}, {u"97,25", 97.25}, {u" 120. \t", 120.0},
             {u"\u00a0\u2009120\u3000", 120.0}, {u"0.01", 0.01}, {u"10000", 10000.0}}) {
        const auto parsed = TempoEditor::parse(text);
        CHECK(parsed.has_value());
        if (parsed) CHECK_NEAR(*parsed, expected, 1e-12);
    }
}

TEST(tempoeditor, rejects_partial_malformed_out_of_range_and_excess_precision_input)
{
    for (const auto* text : {u"", u" ", u".", u".5", u"+120", u"-120", u"1e2", u"NaN",
                             u"Infinity", u"0", u"0.001", u"10000.01", u"120.123",
                             u"12 0", u"12,3.4", u"9999999999999999999999999999999999999999"})
        CHECK(!TempoEditor::parse(text));
}

TEST(tempoeditor, pending_edits_survive_external_updates_and_restore_to_authoritative_value)
{
    TempoEditor input;
    input.setEditing(true);
    input.setText(u"137,5");
    input.setValue(95.0);
    CHECK(input.text() == u"137,5");
    CHECK(input.commit() == 137.5);

    // A rejected proposal must restore the accepted engine value, not the proposal.
    input.restoreText();
    CHECK(input.text() == u"95");
    input.setText(u"0");
    CHECK(!input.acceptableInput());
    CHECK(!input.commit());
    input.restoreText();
    CHECK(input.text() == u"95");

    input.setEditing(false);
    input.setValue(97.25);
    CHECK(input.text() == u"97.25");
    CHECK(!input.commit());
    input.setValue(std::numeric_limits<double>::infinity());
    CHECK(input.value() == 97.25);
}

TEST(tempoeditor, steps_use_pending_input_or_current_value_and_clamp_at_both_limits)
{
    TempoEditor input;
    input.setText(u"137,5");
    CHECK(input.adjust(1) == 138.5);
    input.setText(u"invalid");
    CHECK(input.adjust(-1) == 119.0);
    input.setValue(10000);
    CHECK(!input.adjust(1));
    input.setValue(0.01);
    CHECK(!input.adjust(-1));
    input.setText(u"0.02");
    CHECK(!input.adjust(-1));
}
