#include "testkit.h"
#include "instruments/guitar/plugins/tuner/tunerstate.h"
#include "instruments/guitar/plugins/tuner/tuner.h"

TEST(tunerstate, bypassed_or_silent_returns_waiting_state) {
    auto silent = TunerState::evaluate(0.0f, 1, 0, false);
    CHECK(silent.frequency == 0.0f);
    CHECK(silent.revision == 1);
    CHECK(silent.note == "—");
    CHECK(silent.cents == 0.0);
    CHECK(silent.direction == "waiting");
    CHECK(!silent.hasPitch);
    CHECK(!silent.inTune);

    auto bypassed = TunerState::evaluate(440.0f, 2, 0, true);
    CHECK(bypassed.frequency == 0.0f);
    CHECK(bypassed.direction == "waiting");
    CHECK(!bypassed.hasPitch);
}

TEST(tunerstate, in_tune_threshold_within_three_cents) {
    // A2 standard pitch is 110.0 Hz
    auto inTune = TunerState::evaluate(110.0f, 10, 0, false);
    CHECK(inTune.hasPitch);
    CHECK(inTune.note == "A2");
    CHECK(inTune.stringNumber == 5);
    CHECK(std::abs(inTune.cents) <= TunerState::InTuneThresholdCents);
    CHECK(inTune.inTune);
    CHECK(inTune.direction == "inTune");

    // Slightly flat: 109.5 Hz (~ -7.88 cents)
    auto flat = TunerState::evaluate(109.5f, 11, 0, false);
    CHECK(flat.hasPitch);
    CHECK(flat.note == "A2");
    CHECK(flat.cents < -TunerState::InTuneThresholdCents);
    CHECK(!flat.inTune);
    CHECK(flat.direction == "low");

    // Slightly sharp: 110.5 Hz (~ +7.85 cents)
    auto sharp = TunerState::evaluate(110.5f, 12, 0, false);
    CHECK(sharp.hasPitch);
    CHECK(sharp.note == "A2");
    CHECK(sharp.cents > TunerState::InTuneThresholdCents);
    CHECK(!sharp.inTune);
    CHECK(sharp.direction == "high");
}

TEST(tunerstate, target_string_selection) {
    // E4 (high E, string 1) standard pitch is ~329.63 Hz
    auto autoE4 = TunerState::evaluate(329.63f, 1, 0, false);
    CHECK(autoE4.note == "E4");
    CHECK(autoE4.stringNumber == 1);

    // E2 (low E, string 6) standard pitch is ~82.41 Hz
    auto autoE2 = TunerState::evaluate(82.41f, 1, 0, false);
    CHECK(autoE2.note == "E2");
    CHECK(autoE2.stringNumber == 6);

    // Force selection of string 6 (index 0) even if pitch is slightly off
    auto forced = TunerState::evaluate(85.0f, 2, 6, false);
    CHECK(forced.targetSelection == 6);
    CHECK(forced.note == "E4"); // string 6 in standardtuning targets is E4 (index 5)
}

TEST(tunerstate, freshness_evaluation) {
    TunerState state;
    Tuner tuner;
    state.update(tuner, false, 1.0);

    // If revision changed, it is fresh regardless of elapsed time
    CHECK(state.isFresh(999, 10.0));

    // If revision is the same, fresh only within maxAge
    CHECK(state.isFresh(state.current().revision, 0.2, 0.5));
    CHECK(!state.isFresh(state.current().revision, 0.6, 0.5));
}
