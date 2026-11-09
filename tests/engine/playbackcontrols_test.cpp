#include "testkit.h"
#include "engine/playbackcontrols.h"
#include "core/valueformat.h"

#include <cmath>
#include <limits>

TEST(playbackcontrols, state_transitions)
{
    PlaybackControls ctrl;
    CHECK(ctrl.state() == PlaybackControls::State::Stopped);
    CHECK(!ctrl.isPlaying());
    CHECK(!ctrl.isRecording());

    ctrl.setState(PlaybackControls::State::Playing);
    CHECK(ctrl.isPlaying());
    CHECK(!ctrl.isRecording());

    ctrl.setState(PlaybackControls::State::Recording);
    CHECK(ctrl.isPlaying());
    CHECK(ctrl.isRecording());

    ctrl.setState(PlaybackControls::State::Paused);
    CHECK(!ctrl.isPlaying());
    CHECK(!ctrl.isRecording());

    ctrl.setRepeating(true);
    CHECK(ctrl.isRepeating());
    ctrl.setRepeating(false);
    CHECK(!ctrl.isRepeating());

    ctrl.setCountingIn(true);
    CHECK(ctrl.isCountingIn());
}

TEST(playbackcontrols, position_conversions)
{
    const double sampleRate = 48000.0;
    const double bpm = 120.0; // 2 beats per second -> 24000 samples per beat

    CHECK(PlaybackControls::beatsToSamples(1.0, sampleRate, bpm) == 24000);
    CHECK(PlaybackControls::samplesToBeats(24000, sampleRate, bpm) == 1.0);
    CHECK(PlaybackControls::beatsToSamples(4.0, sampleRate, bpm) == 96000);

    CHECK(PlaybackControls::samplesToSeconds(48000, sampleRate) == 1.0);
    CHECK(PlaybackControls::secondsToSamples(1.5, sampleRate) == 72000);

    CHECK(PlaybackControls::calculateForwardSeekBeat(0.0, 4, 4) == 4.0);
    CHECK(PlaybackControls::calculateForwardSeekBeat(2.0, 3, 4) == 5.0);
}

TEST(playbackcontrols, formatting)
{
    CHECK(PlaybackControls::formatTime(0.0) == "00:00:00.00");
    CHECK(PlaybackControls::formatTime(65.432) == "00:01:05.43");
    CHECK(PlaybackControls::formatTime(3661.129) == "01:01:01.12");  // floored, never rounded up
    CHECK(PlaybackControls::formatTime(0.29) == "00:00:00.29");      // 0.29 * 100 is 28.999...
    // One clock format (valueformat::position); the transport clock reads zero before the start
    // or without a position, and a time too large to count no longer overflows.
    for (const double s : {0.0, 0.29, 59.9996, 65.432, 3661.129, 360000.0})
        CHECK(PlaybackControls::formatTime(s) == valueformat::position(s));
    CHECK(PlaybackControls::formatTime(-1.5) == "00:00:00.00");
    CHECK(PlaybackControls::formatTime(std::numeric_limits<double>::quiet_NaN()) == "00:00:00.00");
    CHECK(PlaybackControls::formatTime(std::numeric_limits<double>::infinity()) == "00:00:00.00");
    CHECK(PlaybackControls::formatTime(1e99) == valueformat::position(1e99));

    CHECK(PlaybackControls::formatBarsBeats(0.0, 4, 4) == "1.1.00");
    CHECK(PlaybackControls::formatBarsBeats(4.0, 4, 4) == "2.1.00");
    CHECK(PlaybackControls::formatBarsBeats(5.5, 4, 4) == "2.2.50");
    CHECK(PlaybackControls::formatBarsBeats(3.0, 3, 4) == "2.1.00");
    // 6/8: a bar is three quarter notes, counted as six eighth-note beats.
    CHECK(PlaybackControls::formatBarsBeats(0.5, 6, 8) == "1.2.00");
    CHECK(PlaybackControls::formatBarsBeats(2.75, 6, 8) == "1.6.50");
    CHECK(PlaybackControls::formatBarsBeats(3.0, 6, 8) == "2.1.00");
    // Before the start (count-in pre-roll) and invalid input read as the start.
    CHECK(PlaybackControls::formatBarsBeats(-2.0, 4, 4) == "1.1.00");
    CHECK(PlaybackControls::formatBarsBeats(std::nan(""), 4, 4) == "1.1.00");
    CHECK(PlaybackControls::formatBarsBeats(1.0, 0, 0) == "1.2.00");
}

TEST(playbackcontrols, repeat_range_and_loop_calculation)
{
    PlaybackControls ctrl;
    const double sampleRate = 48000.0;
    const double bpm = 120.0;

    CHECK(!ctrl.hasExplicitRepeatRange());
    auto autoLoop = ctrl.calculateLoopSamples(8.0, 4, 4, sampleRate, bpm);
    CHECK(autoLoop.isValid);
    CHECK(autoLoop.startSample == 0);
    CHECK(autoLoop.endSample == 8 * 24000);

    CHECK(ctrl.setRepeatRange(2.0, 6.0, sampleRate, bpm));
    CHECK(ctrl.hasExplicitRepeatRange());
    CHECK(ctrl.repeatRange().startBeat == 2.0);
    CHECK(ctrl.repeatRange().endBeat == 6.0);

    auto explicitLoop = ctrl.calculateLoopSamples(12.0, 4, 4, sampleRate, bpm);
    CHECK(explicitLoop.isValid);
    CHECK(explicitLoop.startSample == 2 * 24000);
    CHECK(explicitLoop.endSample == 6 * 24000);

    // Invalid range
    CHECK(!ctrl.setRepeatRange(6.0, 2.0, sampleRate, bpm));
    CHECK(!ctrl.setRepeatRange(-1.0, 4.0, sampleRate, bpm));
}

TEST(playbackcontrols, action_evaluation)
{
    PlaybackControls ctrl;
    const double sampleRate = 48000.0;
    const double bpm = 120.0;

    // Play without audio
    auto plan1 = ctrl.evaluateAction(PlaybackControls::Action::Play, 0.0, sampleRate, bpm,
                                     4, 4, 0.0, false, true, true, false);
    CHECK(plan1.command == PlaybackControls::ActionPlan::Command::Error);

    // Play with audio ready
    auto plan2 = ctrl.evaluateAction(PlaybackControls::Action::Play, 0.0, sampleRate, bpm,
                                     4, 4, 0.0, true, true, true, false);
    CHECK(plan2.command == PlaybackControls::ActionPlan::Command::Play);

    // Pause & Stop
    auto pausePlan = ctrl.evaluateAction(PlaybackControls::Action::Pause, 0.0, sampleRate, bpm,
                                         4, 4, 0.0, true, true, true, false);
    CHECK(pausePlan.command == PlaybackControls::ActionPlan::Command::Pause);

    auto stopPlan = ctrl.evaluateAction(PlaybackControls::Action::Stop, 0.0, sampleRate, bpm,
                                        4, 4, 0.0, true, true, true, false);
    CHECK(stopPlan.command == PlaybackControls::ActionPlan::Command::Stop);

    // Rewind
    auto rewindPlan = ctrl.evaluateAction(PlaybackControls::Action::Rewind, 4.0, sampleRate, bpm,
                                          4, 4, 0.0, true, true, true, false);
    CHECK(rewindPlan.command == PlaybackControls::ActionPlan::Command::Seek);
    CHECK(rewindPlan.argA == 0);

    // Record without input
    auto recPlan1 = ctrl.evaluateAction(PlaybackControls::Action::Record, 0.0, sampleRate, bpm,
                                        4, 4, 0.0, true, false, true, false);
    CHECK(recPlan1.command == PlaybackControls::ActionPlan::Command::Error);

    // Record ready
    auto recPlan2 = ctrl.evaluateAction(PlaybackControls::Action::Record, 0.0, sampleRate, bpm,
                                        4, 4, 0.0, true, true, true, false);
    CHECK(recPlan2.command == PlaybackControls::ActionPlan::Command::Record);
}
