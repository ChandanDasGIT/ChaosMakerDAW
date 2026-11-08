#include "guitardefinition.h"

#include <array>

std::span<const InstrumentDefinition> guitardefinition::variants() noexcept
{
    static constexpr std::array<InstrumentDefinition, 2> definitions{{
        {"Acoustic Guitar", "Acoustic Guitar", true, true, "guitarsuite", "Guitar", "Acoustic"},
        {"Electric Guitar", "Electric Guitar", true, true, "guitarsuite", "Guitar", "Electric"}
    }};
    return definitions;
}
