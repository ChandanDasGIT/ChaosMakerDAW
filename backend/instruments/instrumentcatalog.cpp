#include "instrumentcatalog.h"
#include "instruments/guitar/guitardefinition.h"

#include <algorithm>
#include <vector>

std::span<const InstrumentDefinition> instrumentcatalog::instruments()
{
    static const auto catalog = [] {
        std::vector<InstrumentDefinition> entries;
        entries.push_back({"Piano", "Piano", false, false});
        const auto guitars = guitardefinition::variants();
        entries.insert(entries.end(), guitars.begin(), guitars.end());
        entries.push_back({"Strings", "Strings", false, false});
        entries.push_back({"Drums", "Drums", false, false});
        entries.push_back({"Harmonium", "Harmonium", false, false, "harmonium"});
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
