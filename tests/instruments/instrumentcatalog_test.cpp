#include "testkit.h"
#include "instruments/instrumentcatalog.h"
#include "instruments/guitar/guitardefinition.h"

#include <array>
#include <set>

TEST(instrumentcatalog, existing_track_type_ids_names_and_choice_order_are_preserved)
{
    constexpr std::array<std::string_view, 9> expected{
        "Piano", "Acoustic Guitar", "Electric Guitar", "Strings", "Drums", "Harmonium", "Electronic Drums", "Synth",
        "Chords"
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

TEST(instrumentcatalog, instruments_with_controls_name_their_fx_page)
{
    CHECK(instrumentcatalog::find("Electric Guitar")->page == "guitarsuite");
    CHECK(instrumentcatalog::find("Acoustic Guitar")->page == "guitarsuite");
    CHECK(instrumentcatalog::find("Harmonium")->page == "harmonium");
    CHECK(instrumentcatalog::find("Drums")->page == "drumkit");
    CHECK(instrumentcatalog::find("Electronic Drums")->page == "electrodrums");
    CHECK(instrumentcatalog::find("Synth")->page == "synth");
    CHECK(!instrumentcatalog::find("Synth")->ownKind && instrumentcatalog::find("Synth")->group.empty());
    CHECK(instrumentcatalog::find("Piano")->page == "piano");
    CHECK(instrumentcatalog::find("Strings")->page.empty());
    // The Chords track: its strips, then the grand piano it plays through; listed on its own.
    const auto* chordsTrack = instrumentcatalog::find("Chords");
    CHECK(chordsTrack && chordsTrack->page == "chords" && chordsTrack->extraPage == "piano" && chordsTrack->ownKind);
    for (const auto& entry : instrumentcatalog::instruments())
        if (entry.id != "Chords") CHECK(!entry.ownKind && entry.extraPage.empty());
    CHECK(instrumentcatalog::namesKeys("Chords") && instrumentcatalog::namesKeys("Drums") && !instrumentcatalog::namesKeys("Piano"));
    CHECK(!instrumentcatalog::isDrumKit("Chords"));
}

TEST(instrumentcatalog, drum_kits_name_their_keys_from_their_own_maps)
{
    CHECK(instrumentcatalog::isDrumKit("Drums") && instrumentcatalog::isDrumKit("Electronic Drums"));
    CHECK(!instrumentcatalog::isDrumKit("Piano") && !instrumentcatalog::isDrumKit("Harmonium"));
    CHECK(instrumentcatalog::drumName("Drums", 60) == "Kick");
    CHECK(instrumentcatalog::drumName("Electronic Drums", 60) == "Kick");
    CHECK(instrumentcatalog::drumName("Drums", 63).empty());              // no clap in the acoustic kit
    CHECK(instrumentcatalog::drumName("Electronic Drums", 63) == "Clap");
    CHECK(instrumentcatalog::drumName("Electronic Drums", 73) == "Crash");
    CHECK(instrumentcatalog::drumName("Piano", 60).empty());
}

TEST(instrumentcatalog, guitars_and_drums_are_grouped_by_kind)
{
    const auto kind = [](std::string_view id) {
        const auto* e = instrumentcatalog::find(id);
        return e ? std::pair{e->group, e->variant} : std::pair<std::string_view, std::string_view>{};
    };
    CHECK(kind("Acoustic Guitar") == (std::pair<std::string_view, std::string_view>{"Guitar", "Acoustic"}));
    CHECK(kind("Electric Guitar") == (std::pair<std::string_view, std::string_view>{"Guitar", "Electric"}));
    CHECK(kind("Drums") == (std::pair<std::string_view, std::string_view>{"Drums", "Acoustic"}));
    CHECK(kind("Electronic Drums") == (std::pair<std::string_view, std::string_view>{"Drums", "Electronic"}));
    for (const char* id : {"Piano", "Strings", "Harmonium"})
        CHECK(kind(id) == (std::pair<std::string_view, std::string_view>{"", ""}));
}
