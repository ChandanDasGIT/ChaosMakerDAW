#pragma once

#include <string>

// Where the sample kits (drum kit, grand piano) are, for every instrument that plays them.
namespace samplekits {
// $CHAOSMAKER_SAMPLE_KITS when set (used as given, even if it does not exist); else
// resources/sample-kits up to three folders above the program (a build in the source tree);
// else the installed copy in userFolder(). Empty when none exists.
std::string folder();
// Where an install puts the kits: $XDG_DATA_HOME/ChaosMaker/sample-kits, or
// ~/.local/share/ChaosMaker/sample-kits.
std::string userFolder();
}
