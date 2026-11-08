#include "instrumentcatalog.h"
#include "instruments/drums/drummap.h"
#include "instruments/electrodrums/electromap.h"
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
        entries.push_back({"Drums", "Drums", false, false, "drumkit", "Drums", "Acoustic"});
        entries.push_back({"Harmonium", "Harmonium", false, false, "harmonium"});
        entries.push_back({"Electronic Drums", "Electronic Drums", false, false, "electrodrums", "Drums", "Electronic"});
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

bool instrumentcatalog::isDrumKit(std::string_view id) noexcept
{
    return id == "Drums" || id == "Electronic Drums";
}

std::string_view instrumentcatalog::drumName(std::string_view id, int key) noexcept
{
    if (id == "Drums") return drummap::nameOf(key);
    if (id == "Electronic Drums") return electromap::nameOf(key);
    return {};
}
