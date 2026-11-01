#include "testkit.h"
#include "testsignals.h"
#include "plugins/noisegate/noisegate.h"
#include "plugins/compressor/compressor.h"
#include "plugins/wah/wah.h"
#include "plugins/chorus/chorus.h"
#include "plugins/tremolo/tremolo.h"
#include "plugins/tuner/tuner.h"
#include "plugins/tuner/standardtuning.h"
#include "plugins/delay/delaypedal.h"
#include "plugins/guitarsuite/guitarsuite.h"
#include <memory>

namespace {
void analyse(Processor& processor, ProcessContext& context) noexcept RT_SAFE { processor.analyseInput(context); }
void set(Processor& p, const char* id, float value) {
    for (auto& control : p.parameters()) if (control->id == id) control->set(value);
}
double tailRms(std::vector<float> v) {
    return testsignals::rmsDb({v.begin() + v.size()/2, v.end()});
}
}
TEST(phase2, gate_rejects_noise_and_opens_for_notes) {
    NoiseGate gate;
    const auto quiet = testsignals::render(gate, testsignals::sine(220, -70, 48000), 128);
    CHECK(tailRms(quiet) < -110);
    const auto note = testsignals::sine(220, -12, 48000);
    CHECK_NEAR(tailRms(testsignals::render(gate, note, 128)), tailRms(note), 0.05);
}
TEST(phase2, compressor_reduces_peaks_and_unity_ratio_is_dry) {
    Compressor compressor;
    auto input = testsignals::sine(220, -6, 48000);
    const auto compressed = testsignals::render(compressor, input, 128);
    CHECK(tailRms(compressed) < tailRms(input) - 8);
    set(compressor, "ratio", 1);
    CHECK(testsignals::render(compressor, input, 128) == input);
}
TEST(phase2, wah_position_moves_the_passband) {
    Wah wah;
    const auto input = testsignals::sine(350, -12, 24000);
    set(wah, "position", 0);
    const double low = tailRms(testsignals::render(wah, input, 128));
    set(wah, "position", 1);
    const double high = tailRms(testsignals::render(wah, input, 128));
    CHECK(low > high + 7);
}
TEST(phase2, modulation_changes_signal_and_zero_amount_is_dry) {
    Chorus chorus; Tremolo tremolo;
    const auto input = testsignals::sine(220, -12, 48000);
    CHECK(testsignals::render(chorus, input, 128) != input);
    CHECK(tailRms(testsignals::render(tremolo, input, 128)) < tailRms(input) - 1);
    set(chorus, "mix", 0); set(tremolo, "depth", 0);
    CHECK(testsignals::render(chorus, input, 128) == input);
    CHECK(testsignals::render(tremolo, input, 128) == input);
}
TEST(phase2, tuner_tracks_guitar_notes_without_changing_samples) {
    Tuner tuner;
    for (double hz : {65.406, 82.407, 110.0, 146.832, 195.998, 246.942, 329.628, 440.0, 880.0}) {
        const auto input = testsignals::sine(hz, -18, 48000);
        CHECK(testsignals::render(tuner, input, 128) == input);
        CHECK(std::abs(1200 * std::log2(tuner.frequency() / hz)) < 5);
    }
    testsignals::render(tuner, std::vector<float>(48000), 128);
    CHECK(tuner.frequency() == 0);
}
TEST(phase2, tuner_reads_input_independently_of_chain_order) {
    auto tuner = std::make_shared<Tuner>();
    auto slot = std::make_shared<PedalSlot>(); slot->processor = tuner;
    GuitarSuite suite({slot}); suite.prepare(48000, 128);
    auto input = testsignals::sine(110, -12, 48000);
    for (int pos=0; pos<int(input.size()); pos+=128) {
        float* channels[]{input.data()+pos};
        ProcessContext context; context.audio = {channels, 1, std::min(128, int(input.size())-pos)};
        analyse(suite, context);
    }
    CHECK_NEAR(tuner->frequency(), 110, 0.2);
    const auto revision = tuner->revision();
    auto output = testsignals::sine(440, -12, 128);
    float* channels[]{output.data()}; ProcessContext ctx; ctx.audio = {channels, 1, 128};
    testsignals::rtProcess(suite, ctx);
    CHECK(tuner->revision() == revision);
    slot->bypassed.store(true); analyse(suite, ctx);
    CHECK(tuner->frequency() == 0);
}
TEST(phase2, all_effects_are_block_independent_and_finite_at_extremes) {
    std::vector<std::unique_ptr<Processor>> pedals;
    pedals.push_back(std::make_unique<NoiseGate>()); pedals.push_back(std::make_unique<Compressor>());
    pedals.push_back(std::make_unique<Wah>()); pedals.push_back(std::make_unique<Chorus>());
    pedals.push_back(std::make_unique<Tremolo>()); pedals.push_back(std::make_unique<Tuner>());
    const auto input = testsignals::sine(220, -6, 16000);
    for (auto& pedal : pedals) {
        CHECK(testsignals::render(*pedal, input, 32) == testsignals::render(*pedal, input, 257));
        for (auto& param : pedal->parameters()) param->set(param->max);
        const auto output = testsignals::render(*pedal, input, 64);
        for (float sample : output) CHECK(std::isfinite(sample) && std::abs(sample) < 16);
    }
}
TEST(phase2, stereo_compression_is_linked_and_chorus_spreads_channels) {
    Compressor compressor;
    Chorus chorus;
    for (Processor* processor : std::vector<Processor*>{&compressor, &chorus}) {
        processor->prepare(48000, 128);
        auto left = testsignals::sine(220, -30, 24000);
        auto right = testsignals::sine(220, processor == &compressor ? -6 : -30, 24000);
        const auto dryLeft = left;
        for (int pos = 0; pos < int(left.size()); pos += 128) {
            float* channels[]{left.data()+pos, right.data()+pos};
            ProcessContext ctx; ctx.audio = {channels, 2, std::min(128, int(left.size())-pos)};
            testsignals::rtProcess(*processor, ctx);
        }
        if (processor == &compressor) {
            CHECK(tailRms(left) < tailRms(dryLeft) - 8);
            CHECK_NEAR(tailRms(right) - tailRms(left), 24, 0.01);
        } else {
            CHECK(left != right);
        }
    }
}
TEST(phase2, tuner_supports_interface_sample_rates_and_harmonics) {
    for (double rate : {44100.0, 96000.0}) {
        Tuner tuner; tuner.prepare(rate, 128);
        for (int pos = 0; pos < int(rate); pos += 128) {
            float samples[128];
            for (int i = 0; i < 128; ++i) {
                const double phase = testsignals::kTwoPi * 82.407 * (pos+i) / rate;
                samples[i] = float(0.2*std::sin(phase) + 0.1*std::sin(phase*2) + 0.05*std::sin(phase*3));
            }
            float* channels[]{samples}; ProcessContext ctx; ctx.audio = {channels, 1, 128}; ctx.sampleRate = rate;
            testsignals::rtProcess(tuner, ctx);
        }
        CHECK(std::abs(1200 * std::log2(tuner.frequency() / 82.407)) < 5);
    }
}

TEST(phase2, wah_preserves_broadband_signal_and_upstream_delay) {
    Wah wah;
    for (float position : {0.0f, 0.5f, 1.0f}) {
        set(wah, "position", position);
        for (double frequency : {82.407, 220.0, 1000.0, 6000.0}) {
            const auto input = testsignals::sine(frequency, -18, 24000);
            CHECK(tailRms(testsignals::render(wah, input, 128)) >= tailRms(input) - 0.1);
        }
    }
    auto delay = std::make_shared<DelayPedal>();
    set(*delay, "time", 80); set(*delay, "mix", 0.5f);
    auto delaySlot = std::make_shared<PedalSlot>(); delaySlot->processor = delay;
    auto wahSlot = std::make_shared<PedalSlot>(); wahSlot->processor = std::make_shared<Wah>();
    GuitarSuite chain({delaySlot, wahSlot});
    auto input = testsignals::sine(220, -18, 4800); input.resize(12000, 0);
    const auto output = testsignals::render(chain, input, 128);
    CHECK(testsignals::rmsDb({output.begin()+6000, output.begin()+9000}) > -50);
}
TEST(phase2, standard_tuning_reports_flat_sharp_and_exact_open_strings) {
    for (int i = 0; i < 6; ++i) {
        const double target = 440 * std::pow(2.0, (standardtuning::targets[i].midi - 69) / 12.0);
        for (double cents : {-25.0, 0.0, 25.0}) {
            auto reading = standardtuning::measure(target * std::pow(2.0, cents / 1200));
            CHECK(reading.index == i);
            CHECK_NEAR(reading.targetHz, target, 1e-6);
            CHECK_NEAR(reading.cents, cents, 1e-6);
        }
    }
    // Selecting a string keeps that target even when the input is badly detuned.
    CHECK_NEAR(standardtuning::measure(110, 1).cents, 500, 1e-6);
    CHECK(standardtuning::measure(0).index == -1);
    CHECK(standardtuning::measure(std::nan("")).index == -1);
    Tuner tuner; set(tuner, "string", 6);
    CHECK(tuner.selectedString() == 6);
}
