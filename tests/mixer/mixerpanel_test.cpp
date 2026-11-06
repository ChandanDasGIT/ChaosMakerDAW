#include "testkit.h"
#include "mixer/mixerpanel.h"
#include <limits>

TEST(mixerpanel, volume_clamping_and_conversion)
{
    CHECK_NEAR(mixer::clampVolume(1.0), 1.0, 1e-9);
    CHECK_NEAR(mixer::clampVolume(-1.0), 0.0, 1e-9);
    CHECK_NEAR(mixer::clampVolume(3.5), 2.0, 1e-9);
    CHECK_NEAR(mixer::clampVolume(std::numeric_limits<double>::quiet_NaN()), 0.0, 1e-9);

    CHECK_NEAR(mixer::volumeToDb(1.0), 0.0, 1e-9);
    CHECK_NEAR(mixer::volumeToDb(0.5), -6.0205999, 1e-3);
    CHECK(mixer::volumeToDb(0.0) == levelscale::silenceDb);

    CHECK_NEAR(mixer::dbToVolume(0.0), 1.0, 1e-9);
    CHECK_NEAR(mixer::dbToVolume(levelscale::gainToDb(1.5)), 1.5, 1e-9);
    CHECK_NEAR(mixer::dbToVolume(100.0), 2.0, 1e-9); // clamped to maxVolumeGain
}

TEST(mixerpanel, strip_width_clamping)
{
    CHECK_NEAR(mixer::clampStripWidth(108.0), 108.0, 1e-9);
    CHECK_NEAR(mixer::clampStripWidth(50.0), 82.0, 1e-9);
    CHECK_NEAR(mixer::clampStripWidth(300.0), 220.0, 1e-9);
    CHECK_NEAR(mixer::clampStripWidth(std::numeric_limits<double>::quiet_NaN()), 82.0, 1e-9);
}

TEST(mixerpanel, formatting_and_tooltips)
{
    CHECK(mixer::formatVolumeDb(0.0) == "\xe2\x88\x92\xe2\x88\x9e dB");
    CHECK(mixer::formatVolumeDb(1.0) == "0.0 dB");

    CHECK(mixer::formatVolumeTooltip(1.0) == "Track volume 100%; double-click for 0 dB");
    CHECK(mixer::formatVolumeTooltip(0.5) == "Track volume 50%; double-click for 0 dB");

    CHECK(mixer::formatMuteTooltip(true) == "Mute enabled");
    CHECK(mixer::formatMuteTooltip(false) == "Mute disabled");

    CHECK(mixer::formatSoloTooltip(true) == "Solo enabled");
    CHECK(mixer::formatSoloTooltip(false) == "Solo disabled");
}

TEST(mixerpanel, clip_gain_clamping_and_formatting)
{
    CHECK_NEAR(mixer::clampClipGain(0.0), 0.0, 1e-9);
    CHECK_NEAR(mixer::clampClipGain(-100.0), -24.0, 1e-9);
    CHECK_NEAR(mixer::clampClipGain(50.0), 24.0, 1e-9);
    CHECK_NEAR(mixer::clampClipGain(std::numeric_limits<double>::quiet_NaN()), -24.0, 1e-9);

    CHECK(mixer::formatClipGainDb(0.0) == "0.0 dB");
    CHECK(mixer::formatClipGainDb(3.5) == "+3.5 dB");
    CHECK(mixer::formatClipGainDb(-6.0) == "-6.0 dB");
    CHECK(mixer::formatClipGainDb(-24.0) == "-24.0 dB");
    CHECK(mixer::formatClipGainDb(std::numeric_limits<double>::quiet_NaN()) == "\xe2\x80\x94");

    CHECK(mixer::formatClipGainTooltip(0.0) == "Clip gain 0.0 dB; double-click for 0 dB");
    CHECK(mixer::formatClipGainTooltip(3.0) == "Clip gain +3.0 dB; double-click for 0 dB");
    CHECK(mixer::formatClipGainTooltip(-12.0) == "Clip gain -12.0 dB; double-click for 0 dB");
    CHECK(mixer::formatClipGainTooltip(-24.0) == "Clip gain -24.0 dB; double-click for 0 dB");
}

TEST(mixerpanel, make_strip_state)
{
    const auto strip = mixer::MixerPanel::makeStrip(42, "Vocals", "#ff5500", false, true, 1.25, 120.0, 3.5);
    CHECK(strip.trackId == 42);
    CHECK(strip.name == "Vocals");
    CHECK(strip.colour == "#ff5500");
    CHECK(!strip.isMuted);
    CHECK(strip.isSolo);
    CHECK_NEAR(strip.volume, 1.25, 1e-9);
    CHECK_NEAR(strip.clipGain, 3.5, 1e-9);
    CHECK_NEAR(strip.stripWidth, 120.0, 1e-9);
    CHECK(!strip.volumeText.empty());
    CHECK(strip.clipGainText == "+3.5 dB");
    CHECK(strip.clipGainTooltip == "Clip gain +3.5 dB; double-click for 0 dB");
    CHECK(strip.volumeTooltip == "Track volume 125%; double-click for 0 dB");
    CHECK(strip.muteTooltip == "Mute disabled");
    CHECK(strip.soloTooltip == "Solo enabled");
}

TEST(mixerpanel, make_master_strip_state)
{
    const auto master = mixer::MixerPanel::makeMasterStrip(false, 1.0, 110.0);
    CHECK(master.name == "Master");
    CHECK(!master.colour.empty());
    CHECK(!master.isMuted);
    CHECK_NEAR(master.volume, 1.0, 1e-9);
    CHECK_NEAR(master.stripWidth, 110.0, 1e-9);
    CHECK(master.volumeText == "0.0 dB");
    CHECK(master.volumeTooltip == "Master volume 100%; double-click for 0 dB");
    CHECK(master.muteTooltip == "Master mute disabled");

    const auto mutedMaster = mixer::MixerPanel::makeMasterStrip(true, 0.5, 96.0);
    CHECK(mutedMaster.isMuted);
    CHECK_NEAR(mutedMaster.volume, 0.5, 1e-9);
    CHECK(mutedMaster.muteTooltip == "Master mute enabled");
    CHECK(mutedMaster.volumeTooltip == "Master volume 50%; double-click for 0 dB");
}

TEST(mixerpanel, clip_gain_fader_targets_the_selected_clip_else_the_first)
{
    CHECK(mixer::chooseTargetClip({}, 5) == -1);                  // no clips
    CHECK(mixer::chooseTargetClip({4, 7, 9}, -1) == 4);           // nothing selected: first
    CHECK(mixer::chooseTargetClip({4, 7, 9}, 9) == 9);            // selected clip on this track
    CHECK(mixer::chooseTargetClip({4, 7, 9}, 12) == 4);           // selected clip is elsewhere
}
