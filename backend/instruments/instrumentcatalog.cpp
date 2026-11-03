#include "instrumentcatalog.h"
#include "instruments/guitar/guitardefinition.h"

#include <algorithm>
#include <vector>

std::span<const InstrumentDefinition> instrumentcatalog::instruments()
{
    static const auto catalog = [] {
        const auto guitars = guitardefinition::variants();
        std::vector<InstrumentDefinition> entries(guitars.begin(), guitars.end());
        // Preserve the remaining choices already offered by TrackListWrapper.
        entries.push_back({"Strings", "Strings", false, false});
        entries.push_back({"Drums", "Drums", false, false});
        return entries;
    }();
    return catalog;
}

const InstrumentDefinition* instrumentcatalog::find(std::string_view id)
{
    const auto entries = instruments();
    const auto found = std::find_if(entries.begin(), entries.end(),
                                     [id](const auto& entry) { return entry.id == id; });
    return found == entries.end() ? nullptr : &*found;
}
