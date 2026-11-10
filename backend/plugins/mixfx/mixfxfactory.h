#pragma once

#include "plugins/mixfx/mixeffect.h"

#include <memory>
#include <string_view>

// Makes a mixing plugin's processor by its type (restMap G7). Null for a type that is not a
// mixing plugin.
namespace mixfx {
bool isMixEffect(std::string_view type);
std::shared_ptr<MixEffect> create(std::string_view type);
}
