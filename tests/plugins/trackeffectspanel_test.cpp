#include "testkit.h"
#include "plugins/trackeffectspanel.h"

#include <string>
#include <vector>

TEST(trackeffectspanel, selection_follows_the_track_chain) {
    const std::vector<std::string> chain{"eq", "guitarsuite"};
    CHECK(TrackEffectsPanel::validateSelectedEffect("guitarsuite", chain) == "guitarsuite");
    CHECK(TrackEffectsPanel::validateSelectedEffect("reverb", chain) == "eq");   // not on the chain: first
    CHECK(TrackEffectsPanel::validateSelectedEffect("eq", {}) == "");            // empty chain: nothing shown
}

TEST(trackeffectspanel, state_and_record_mode_description) {
    TrackEffectsPanel panel;
    panel.setTrackInfo(1, "Guitar Track", "Electric Guitar", true, false);
    panel.setChain({"guitarsuite"});
    CHECK(panel.supportsGuitarSuite());
    CHECK(panel.selectedEffect() == "guitarsuite");
    CHECK(panel.currentRecordModeDescription() == "Print FX · effects are baked into new recordings");

    // An added EQ can be shown; removing the selected effect falls back to the first one left.
    panel.setChain({"guitarsuite", "eq"});
    panel.setSelectedEffect("eq");
    CHECK(panel.selectedEffect() == "eq");
    panel.setChain({"guitarsuite"});
    CHECK(panel.selectedEffect() == "guitarsuite");

    // A vocal starts with no effects: nothing selected, no GuitarSuite.
    panel.setTrackInfo(2, "Vocal Track", "Vocal", true, true);
    panel.setChain({});
    CHECK(!panel.supportsGuitarSuite());
    CHECK(!panel.supportsHarmonium());
    CHECK(panel.selectedEffect().empty());
    panel.setSelectedEffect("guitarsuite");
    CHECK(panel.selectedEffect().empty());
    CHECK(panel.currentRecordModeDescription() == "Dry recording · current effects are applied during playback");
}

TEST(trackeffectspanel, drums_track_opens_on_its_drum_kit_page) {
    TrackEffectsPanel panel;
    panel.setTrackInfo(4, "Drums", "Drums", false, false);
    panel.setChain({"drumkit", "eq"});
    CHECK(panel.supportsDrumKit());
    CHECK(!panel.supportsHarmonium() && !panel.supportsGuitarSuite());
    CHECK(panel.selectedEffect() == "drumkit");
}

TEST(trackeffectspanel, piano_track_opens_on_its_grand_piano_page) {
    TrackEffectsPanel panel;
    panel.setTrackInfo(6, "Keys", "Piano", false, false);
    panel.setChain({"piano", "chaoseq"});
    CHECK(panel.supportsPiano());
    CHECK(!panel.supportsDrumKit() && !panel.supportsElectroDrums() && !panel.supportsHarmonium());
    CHECK(panel.selectedEffect() == "piano");
}

TEST(trackeffectspanel, electronic_drums_track_opens_on_its_own_page) {
    TrackEffectsPanel panel;
    panel.setTrackInfo(5, "Beats", "Electronic Drums", false, false);
    panel.setChain({"electrodrums", "eq"});
    CHECK(panel.supportsElectroDrums());
    CHECK(!panel.supportsDrumKit() && !panel.supportsHarmonium() && !panel.supportsGuitarSuite());
    CHECK(panel.selectedEffect() == "electrodrums");
}

TEST(trackeffectspanel, synth_track_opens_on_its_own_page) {
    TrackEffectsPanel panel;
    panel.setTrackInfo(6, "Lead", "Synth", false, false);
    panel.setChain({"synth", "chaoseq"});
    CHECK(panel.supportsSynth());
    CHECK(!panel.supportsHarmonium() && !panel.supportsPiano() && !panel.supportsGuitarSuite());
    CHECK(panel.selectedEffect() == "synth");
}

TEST(trackeffectspanel, harmonium_track_opens_on_its_harmonium_page) {
    TrackEffectsPanel panel;
    panel.setTrackInfo(3, "Harmonium", "Harmonium", false, false);
    panel.setChain({"harmonium", "chaoseq"});
    CHECK(panel.supportsHarmonium());
    CHECK(!panel.supportsGuitarSuite());
    CHECK(panel.selectedEffect() == "harmonium");
}
