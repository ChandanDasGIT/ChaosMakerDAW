#pragma once

#include <string>
#include <string_view>

namespace synth {
// The text a synth control shows for its value, by the control's kind: "percent", "hz", "time",
// "seconds", "octave", "semitones", "cents", "octaves", "db"; any other kind shows the plain number.
std::string valueText(std::string_view kind, double value);
}
