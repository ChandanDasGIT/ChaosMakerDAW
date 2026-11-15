#pragma once

#include <string>
#include <string_view>

namespace bass {
// The text a bass control shows for its value, by the control's kind: "percent", "seconds",
// "semitones", "db", "pluck", "blend"; any other kind shows the plain number.
std::string valueText(std::string_view kind, double value);
}
