#include "testkit.h"
#include "instruments/instrumentcatalog.h"
#include "instruments/guitar/guitardefinition.h"

#include <array>
#include <set>

TEST(instrumentcatalog, existing_track_type_ids_names_and_choice_order_are_preserved)
{
    constexpr std::array<std::string_view, 5> expected{
        "Piano", "Acoustic Guitar", "Electric Guitar", "Strings", "Drums"
    };
    const auto entries = instrumentcatalog::instruments();
    CHECK(entries.size() == expected.size());
    std::set<std::string_view> ids;
    for (std::size_t i = 0; i < entries.size() && i < expected.size(); ++i) {
        CHECK(entries[i].id == expected[i]);
        CHECK(entries[i].name == expected[i]);
        CHECK(instrumentcatalog::find(expected[i]) == &entries[i]);
        CHECK(ids.insert(entries[i].id).second);
    }
    CHECK(!instrumentcatalog::find(""));
    CHECK(!instrumentcatalog::find("electric guitar"));
    CHECK(!instrumentcatalog::find("Guitar"));
    CHECK(!instrumentcatalog::find("SFZ"));
}

TEST(instrumentcatalog, only_existing_live_guitar_variants_offer_guitar_processing)
{
    for (const auto& entry : instrumentcatalog::instruments()) {
        const bool live = entry.id == "Acoustic Guitar" || entry.id == "Electric Guitar";
        CHECK(entry.hasLiveInput == live);
        CHECK(entry.supportsGuitarSuite == live);
    }
    const auto guitars = guitardefinition::variants();
    CHECK(guitars.size() == 2);
    for (const auto& guitar : guitars) {
        const auto* registered = instrumentcatalog::find(guitar.id);
        CHECK(registered);
        if (registered) {
            CHECK(registered->name == guitar.name);
            CHECK(registered->hasLiveInput == guitar.hasLiveInput);
            CHECK(registered->supportsGuitarSuite == guitar.supportsGuitarSuite);
        }
    }
}
