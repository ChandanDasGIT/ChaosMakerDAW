#pragma once

#include "instruments/instrumentcatalog.h"

namespace guitardefinition {
// Guitar-owned identity and capabilities; no Qt or UI-specific type tests.
std::span<const InstrumentDefinition> variants() noexcept;
}
