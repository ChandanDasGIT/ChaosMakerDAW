#include "testkit.h"
#include "dsp/levelmeter.h"
#include "dsp/stereolevelmeter.h"

TEST(levelmeter, initial_state_and_configuration)
{
    LevelMeter meter("Input", true, -12.0);
    CHECK(meter.label() == "Input");
    CHECK(meter.showTarget() == true);
    CHECK_NEAR(meter.calibrationTargetDb(), -12.0, 1e-9);
    CHECK_NEAR(meter.normalizedTarget(), 0.8, 1e-9);

    const auto& snapshot = meter.currentSnapshot();
    CHECK(snapshot.db == levelscale::silenceDb);
    CHECK(snapshot.normalized == 0.0);
    CHECK(snapshot.levelText == "\xe2\x88\x92\xe2\x88\x9e");
    CHECK(snapshot.classification == levelscale::Band::Normal);
    CHECK(snapshot.clipped == false);
}

TEST(levelmeter, updates_decay_and_classification)
{
    LevelMeter meter("Guitar");
    PeakMeter peak;
    peak.capturePeak(0.5f); // ~ -6.02 dB
    auto snapshot = meter.update(peak, 0.05);

    CHECK_NEAR(snapshot.db, -6.0205999, 1e-3);
    CHECK(snapshot.classification == levelscale::Band::Warning);
    CHECK(snapshot.clipped == false);
    CHECK(snapshot.levelText == "-6.0");

    // Clip peak capture
    peak.capturePeak(1.0f);
    snapshot = meter.update(peak, 0.05);
    CHECK_NEAR(snapshot.db, 0.0, 1e-3);
    CHECK(snapshot.classification == levelscale::Band::Clip);
    CHECK(snapshot.clipped == true);
    CHECK(snapshot.levelText == "0.0");

    meter.reset();
    const auto& resetSnap = meter.currentSnapshot();
    CHECK(resetSnap.db == levelscale::silenceDb);
    CHECK(resetSnap.normalized == 0.0);
    CHECK(resetSnap.clipped == false);
}

TEST(stereolevelmeter, initial_state_and_tooltip_formatting)
{
    StereoLevelMeter meter("Master");
    CHECK(meter.label() == "Master");

    const auto& snapshot = meter.currentSnapshot();
    CHECK(snapshot.db[0] == levelscale::silenceDb);
    CHECK(snapshot.db[1] == levelscale::silenceDb);
    CHECK(snapshot.normalized[0] == 0.0);
    CHECK(snapshot.normalized[1] == 0.0);
    CHECK(snapshot.clipped[0] == false);
    CHECK(snapshot.clipped[1] == false);
    CHECK(snapshot.tooltipText == "Master L: \xe2\x88\x92\xe2\x88\x9e dBFS / R: \xe2\x88\x92\xe2\x88\x9e dBFS\nClick to clear clip indicators");
}

TEST(stereolevelmeter, independent_channels_and_clip_latch)
{
    StereoLevelMeter meter("Track 1");
    PeakMeter left, right;
    left.capturePeak(0.5f); // -6.02 dB (warning)
    right.capturePeak(1.0f); // 0.0 dB (clip)

    auto snapshot = meter.update(left, right, 0.05);
    CHECK_NEAR(snapshot.db[0], -6.0205999, 1e-3);
    CHECK_NEAR(snapshot.db[1], 0.0, 1e-3);
    CHECK(snapshot.classification[0] == levelscale::Band::Warning);
    CHECK(snapshot.classification[1] == levelscale::Band::Clip);
    CHECK(snapshot.clipped[0] == false);
    CHECK(snapshot.clipped[1] == true);
    CHECK(snapshot.tooltipText == "Track 1 L: -6.0 dBFS / R: 0.0 dBFS\nClick to clear clip indicators");
}
