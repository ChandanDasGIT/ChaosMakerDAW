#include "guitardefinition.h"

#include <array>

std::span<const InstrumentDefinition> guitardefinition::variants() noexcept
{
    static constexpr std::array<InstrumentDefinition, 3> definitions{{
        {"Guitar", "Guitar", false, false},
        {"Acoustic Guitar", "Acoustic Guitar", true, true},
        {"Electric Guitar", "Electric Guitar", true, true}
    }};
    return definitions;
}
