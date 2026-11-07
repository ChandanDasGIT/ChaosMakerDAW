#include "testkit.h"
#include "instruments/guitar/pedaleditorrules.h"

TEST(pedaleditorrules, pedal_features_by_type)
{
    using pedaleditor::features;
    CHECK(features("pedal.octaver").pitchDelayNote && features("pedal.pitchshift").pitchDelayNote);
    CHECK(!features("pedal.reverb").pitchDelayNote);
    CHECK(features("pedal.cabinet").cabinetIr && !features("pedal.amp").cabinetIr);
    CHECK(features("pedal.tuner").tuner);
    CHECK(features("pedal.tremolo").sustainToggle && !features("pedal.phaser").sustainToggle);
    CHECK(features("pedal.tremolo").tempoSync && features("pedal.phaser").tempoSync && !features("pedal.chorus").tempoSync);
}

TEST(pedaleditorrules, which_parameters_are_dials)
{
    using pedaleditor::isDial;
    CHECK(!isDial("pedal.tuner", "string"));                       // tuner has its own display
    for (const char* id : {"sync", "division", "sustain"}) {
        CHECK(!isDial("pedal.tremolo", id));
        CHECK(!isDial("pedal.phaser", id));
    }
    CHECK(isDial("pedal.tremolo", "rate") && isDial("pedal.phaser", "depth"));
    CHECK(isDial("pedal.chorus", "sync"));                         // only modulation pedals hide it
    CHECK(pedaleditor::disabledWhenSynced("pedal.tremolo", "rate"));
    CHECK(!pedaleditor::disabledWhenSynced("pedal.tremolo", "depth"));
    CHECK(!pedaleditor::disabledWhenSynced("pedal.chorus", "rate"));
    CHECK(pedaleditor::dialStep("pedal.pitchshift", "semitones") == 1.0);
    CHECK(pedaleditor::dialStep("pedal.pitchshift", "mix") == 0.0);
    CHECK(pedaleditor::dialStep("pedal.amp", "channel") == 1.0);     // whole channels
    CHECK(pedaleditor::dialStep("pedal.amp", "drive") == 0.0);
    CHECK(pedaleditor::dialStep("pedal.cabinet", "model") == 1.0);   // whole cab models
}

TEST(pedaleditorrules, value_and_tuner_texts)
{
    CHECK(pedaleditor::formatValue(250.4, "ms") == "250 ms");
    CHECK(pedaleditor::formatValue(-7.0, "st") == "-7 st");
    CHECK(pedaleditor::formatValue(0.255, "") == "0.26");
    CHECK(pedaleditor::formatValue(3.0, "dB") == "3.00 dB");
    CHECK(pedaleditor::formatValue(0.0, "channel") == "Clean");
    CHECK(pedaleditor::formatValue(1.2, "channel") == "Crunch");
    CHECK(pedaleditor::formatValue(2.0, "channel") == "High Gain");
    CHECK(pedaleditor::formatValue(9.0, "channel") == "High Gain");   // out of range: clamped
    CHECK(pedaleditor::formatValue(0.0, "cabmodel") == "None");
    CHECK(pedaleditor::formatValue(1.0, "cabmodel") == "1x12 Open");
    CHECK(pedaleditor::formatValue(2.0, "cabmodel") == "2x12");
    CHECK(pedaleditor::formatValue(3.0, "cabmodel") == "4x12 Closed");
    CHECK(pedaleditor::formatTrim(1.5) == "+1.5 dB" && pedaleditor::formatTrim(0) == "0.0 dB");
    CHECK(pedaleditor::formatTrim(-3) == "-3.0 dB");
    CHECK(pedaleditor::beatDivisionNames().size() == 7);
    CHECK(pedaleditor::beatDivisionNames()[2] == "1/4 · Quarter note");
    CHECK(pedaleditor::syncText(true, 120) == "120 BPM · follows bottom-bar tempo");
    CHECK(pedaleditor::syncText(false, 120) == "Free rate · adjust Rate below");
    const auto strings = pedaleditor::tunerStringLabels();
    CHECK(strings.size() == 6 && strings.front() == "6 · E2" && strings.back() == "1 · E4");
    CHECK(pedaleditor::tunerReferenceText() == "Standard tuning · A4 = 440 Hz");
    CHECK(pedaleditor::inTune(3.0) && pedaleditor::inTune(-2.9) && !pedaleditor::inTune(3.1));
    CHECK(pedaleditor::tunerStatus(false, true, 0) == "Tuner bypassed");
    CHECK(pedaleditor::tunerStatus(false, false, 0) == "Play one open string");
    CHECK(pedaleditor::tunerStatus(true, false, 1.0) == "In tune");
    CHECK(pedaleditor::tunerStatus(true, false, -20) == "LOW / FLAT · Tune up ↑");
    CHECK(pedaleditor::tunerStatus(true, false, 20) == "HIGH / SHARP · Tune down ↓");
    CHECK(pedaleditor::formatCents(2.46) == "+2.5 cents" && pedaleditor::formatCents(-10) == "-10.0 cents");
    CHECK(pedaleditor::formatTunerFrequency(110.24, 110.0) == "110.2 Hz → 110.00 Hz");
}
