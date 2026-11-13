#include "testkit.h"
#include "instruments/guitar/guitarsuiteeditor.h"
#include "instruments/guitar/plugins/guitarsuite/guitarsuite.h"

TEST(guitarsuiteeditor, add_move_remove_and_parameter_operations) {
    GuitarSuiteEditor editor;
    std::shared_ptr<GuitarSuite> publishedChain;
    editor.setPublisher([&](auto chain) { publishedChain = chain; });

    CHECK(editor.count() == 0);
    CHECK(editor.addPedal("pedal.overdrive"));
    CHECK(editor.addPedal("pedal.reverb"));
    CHECK(editor.count() == 2);
    CHECK(publishedChain != nullptr);

    int id0 = editor.slotAt(0)->id;
    int id1 = editor.slotAt(1)->id;
    CHECK(id0 != id1);
    CHECK(editor.slotAt(0)->type == "pedal.overdrive");
    CHECK(editor.slotAt(1)->type == "pedal.reverb");

    // Move
    CHECK(editor.movePedal(0, 1));
    CHECK(editor.slotAt(0)->type == "pedal.reverb");
    CHECK(editor.slotAt(1)->type == "pedal.overdrive");

    // Bypass
    CHECK(editor.setBypassed(id0, true));
    CHECK(editor.findSlot(id0)->bypassed.load());

    // Parameters
    CHECK(editor.setParameter(id0, "drive", 0.75));
    CHECK(editor.setParameter(id0, "nonexistent", 1.0) == false);

    // Input trim clamping
    editor.setInputTrimDb(30.0);
    CHECK(editor.inputTrimDb() == 24.0);
    editor.setInputTrimDb(-30.0);
    CHECK(editor.inputTrimDb() == -24.0);
    editor.setInputTrimDb(-6.0);
    CHECK(editor.inputTrimDb() == -6.0);

    // Remove
    CHECK(editor.removePedal(id1));
    CHECK(editor.count() == 1);
}

TEST(guitarsuiteeditor, session_data_roundtrip) {
    GuitarSuiteEditor editor;
    CHECK(editor.addPedal("pedal.distortion"));
    CHECK(editor.addPedal("pedal.delay"));
    editor.setInputTrimDb(-3.5);
    int distId = editor.slotAt(0)->id;
    CHECK(editor.setBypassed(distId, true));

    std::string session = editor.sessionData();
    CHECK(!session.empty());

    GuitarSuiteEditor restored;
    CHECK(restored.restoreSessionData(session));
    CHECK(restored.count() == 2);
    CHECK(restored.inputTrimDb() == -3.5);
    CHECK(restored.slotAt(0)->type == "pedal.distortion");
    CHECK(restored.slotAt(0)->bypassed.load());
    CHECK(restored.slotAt(1)->type == "pedal.delay");
    CHECK(!restored.slotAt(1)->bypassed.load());
}

TEST(guitarsuiteeditor, the_preset_playing_is_named_marked_edited_and_kept_by_the_session) {
    GuitarSuiteEditor editor;
    CHECK(editor.presetName().empty() && !editor.presetEdited());
    CHECK(editor.addPedal("pedal.overdrive"));
    CHECK(editor.presetName().empty() && !editor.presetEdited());     // a board built by hand has no name

    // A preset taken under a name: that name, not edited.
    auto candidate = editor.preparePresetData(editor.sessionData(), "");
    CHECK(candidate.error.empty() && candidate.name.empty());
    editor.commitPreset(std::move(candidate.candidateSlots), candidate.nextId, candidate.trim, "Found tone");
    CHECK(editor.presetName() == "Found tone" && !editor.presetEdited());
    const int id = editor.slotAt(0)->id;
    // Setting a knob to what it already is, or the bypass to what it is, is no edit.
    const auto& drive = editor.slotAt(0)->processor->parameters().front();
    CHECK(editor.setParameter(id, drive->id, drive->get()));
    CHECK(editor.setBypassed(id, false));
    CHECK(!editor.presetEdited());
    CHECK(editor.setBypassed(id, true));
    CHECK(editor.presetName() == "Found tone" && editor.presetEdited());

    // The session keeps the name and the mark.
    GuitarSuiteEditor restored;
    CHECK(restored.restoreSessionData(editor.sessionData()));
    CHECK(restored.presetName() == "Found tone" && restored.presetEdited());
    // Adding, moving and removing pedals are edits too.
    for (int kind = 0; kind < 3; ++kind) {
        GuitarSuiteEditor other;
        CHECK(other.addPedal("pedal.overdrive") && other.addPedal("pedal.reverb"));
        auto c = other.preparePresetData(other.sessionData(), "");
        other.commitPreset(std::move(c.candidateSlots), c.nextId, c.trim, "Two");
        CHECK(!other.presetEdited());
        if (kind == 0) CHECK(other.addPedal("pedal.delay"));
        if (kind == 1) CHECK(other.movePedal(0, 1));
        if (kind == 2) CHECK(other.removePedal(other.slotAt(0)->id));
        CHECK(other.presetEdited());
        // Not edited round the session: the name alone.
        auto again = other.preparePresetData(other.sessionData(), "");
        other.commitPreset(std::move(again.candidateSlots), again.nextId, again.trim, again.name);
        GuitarSuiteEditor clean;
        CHECK(clean.restoreSessionData(other.sessionData()));
        CHECK(clean.presetName() == "Two" && !clean.presetEdited());
    }
}
