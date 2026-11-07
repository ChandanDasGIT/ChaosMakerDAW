#include "testkit.h"
#include "instruments/drums/drummap.h"

#include <set>

TEST(drummap, every_part_has_one_key_and_a_name)
{
    const auto pads = drummap::pads();
    CHECK(pads.size() == 13);
    std::set<int> keys;
    int previous = -1;
    for (const auto& pad : pads) {
        CHECK(pad.key > previous);                                // low to high, no repeats
        CHECK(!pad.name.empty());
        keys.insert(pad.key);
        previous = pad.key;
    }
    CHECK(drummap::nameOf(60) == "Kick" && drummap::nameOf(62) == "Snare");
    CHECK(drummap::nameOf(66) == "Hi-hat closed" && drummap::nameOf(68) == "Hi-hat pedal");
    CHECK(drummap::nameOf(70) == "Hi-hat open" && drummap::nameOf(55) == "Hi-hat tight");
    CHECK(drummap::nameOf(65) == "Tom low" && drummap::nameOf(69) == "Tom high");
    CHECK(drummap::nameOf(73).empty() && drummap::nameOf(75).empty());   // kept for a crash and a ride
    CHECK(drummap::nameOf(36).empty() && drummap::nameOf(-1).empty());
    // Every part is in the computer keyboard's default reach: Tab (55) .. Backspace (78).
    for (const auto& pad : pads) CHECK(pad.key >= 55 && pad.key <= 78);
}

TEST(drummap, each_hihat_articulation_has_a_pass_onto_a_named_key)
{
    int full = 0;
    std::set<int> targets;
    for (const auto& pass : drummap::passes()) {
        CHECK(pass.hihatPedal >= 0 && pass.hihatPedal <= 127);
        if (pass.fromKey < 0) { ++full; CHECK(pass.toKey < 0); continue; }
        CHECK(pass.fromKey == drummap::KitHihatKey || pass.fromKey == drummap::KitTightHihatKey);
        CHECK(!drummap::nameOf(pass.toKey).empty());
        CHECK(targets.insert(pass.toKey).second);                 // one articulation per key
    }
    CHECK(full == 1);
    CHECK(targets == std::set<int>({55, 56, 57, 66, 70}));
}
