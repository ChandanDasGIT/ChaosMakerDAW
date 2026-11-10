#include "plugins/effectstate.h"

mixfx::Settings EffectState::mixEffect(std::string_view type) const
{
    for (const auto& [stored, settings] : mixEffects)
        if (stored == type) return settings;
    return mixfx::defaults(type);
}
