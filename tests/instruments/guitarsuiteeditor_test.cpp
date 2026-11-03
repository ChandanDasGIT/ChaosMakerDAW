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
