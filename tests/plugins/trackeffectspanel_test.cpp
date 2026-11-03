#include "testkit.h"
#include "plugins/trackeffectspanel.h"

TEST(trackeffectspanel, eligibility_by_track_type_and_input) {
    CHECK(TrackEffectsPanel::checkGuitarSuiteSupport("Electric Guitar", true));
    CHECK(!TrackEffectsPanel::checkGuitarSuiteSupport("Electric Guitar", false));
    CHECK(!TrackEffectsPanel::checkGuitarSuiteSupport("Vocal", true));
    CHECK(!TrackEffectsPanel::checkGuitarSuiteSupport("Vocal", false));
}

TEST(trackeffectspanel, available_entries_and_selection_transitions) {
    // When guitar suite is supported
    const auto entriesWithGuitar = TrackEffectsPanel::availableEntries(true);
    CHECK(entriesWithGuitar.size() == 2);
    CHECK(entriesWithGuitar[0].key == "eq");
    CHECK(entriesWithGuitar[1].key == "pedals");

    // When guitar suite is NOT supported
    const auto entriesWithoutGuitar = TrackEffectsPanel::availableEntries(false);
    CHECK(entriesWithoutGuitar.size() == 1);
    CHECK(entriesWithoutGuitar[0].key == "eq");

    // Transitions
    CHECK(TrackEffectsPanel::validateSelectedEffect("pedals", true) == "pedals");
    CHECK(TrackEffectsPanel::validateSelectedEffect("pedals", false) == "eq");
    CHECK(TrackEffectsPanel::validateSelectedEffect("eq", false) == "eq");
    CHECK(TrackEffectsPanel::validateSelectedEffect("invalid_key", true) == "eq");
}

TEST(trackeffectspanel, state_and_record_mode_description) {
    TrackEffectsPanel panel;
    panel.setTrackInfo(1, "Guitar Track", "Electric Guitar", true, false);
    CHECK(panel.supportsGuitarSuite());
    CHECK(panel.effectEntries().size() == 2);
    CHECK(panel.currentRecordModeDescription() == "Print FX · effects are baked into new recordings");

    panel.setSelectedEffect("pedals");
    CHECK(panel.selectedEffect() == "pedals");

    // Change to vocal track (drops pedals to eq)
    panel.setTrackInfo(2, "Vocal Track", "Vocal", true, true);
    CHECK(!panel.supportsGuitarSuite());
    CHECK(panel.selectedEffect() == "eq");
    CHECK(panel.effectEntries().size() == 1);
    CHECK(panel.currentRecordModeDescription() == "Dry recording · current effects are applied during playback");
}
