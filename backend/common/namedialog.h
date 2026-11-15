#pragma once

#include <string>
#include <string_view>

// The rules of the dialog that names a preset or a groove (save new, rename, replace).
namespace namedialog {
// What a press of Save / Rename / Enter does next.
enum class Step { Nothing, Rename, AskReplace, Save };
// The name with surrounding white space removed.
std::string trimmed(std::string_view name);
// An empty name does nothing; a rename renames; saving over a name of yours first asks to
// replace it, and saves (replacing) on the next press.
Step nextStep(bool renaming, std::string_view name, bool taken, bool confirmedReplace);
}
