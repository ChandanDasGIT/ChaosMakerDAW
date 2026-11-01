// The pattern for testing a plugin. A small gain effect stands in for your real one.
#include "testkit.h"
#include "testsignals.h"

#include "processor/processor.h"
#include "processor/smoother.h"

#include <cmath>

namespace {

class TestGain final : public Processor {
public:
    TestGain() : m_gainDb(addParameter("gain", "Gain", -60.0f, 12.0f, 0.0f, "dB")) {}

    std::string typeId() const override { return "testgain"; }
    void prepare(double sampleRate, int) override { m_smoother.prepare(sampleRate, 20.0f); }
    void reset() noexcept override { m_smoother.reset(targetGain()); }

    void process(ProcessContext& context) noexcept override {
        const float target = targetGain();
        for (int i = 0; i < context.audio.frames; ++i) {
            const float gain = m_smoother.next(target);
            for (int c = 0; c < context.audio.channelCount; ++c) context.audio.channel(c)[i] *= gain;
        }
    }

    Parameter& gainDb() { return m_gainDb; }

private:
    float targetGain() const noexcept { return std::pow(10.0f, m_gainDb.get() / 20.0f); }

    Parameter& m_gainDb;
    Smoother m_smoother;
};

}

// Measure the result of a known signal.
TEST(processor, minus_six_db_lowers_a_sine_by_six_db)
{
    TestGain gain;
    gain.gainDb().set(-6.0f);
    const auto output = testsignals::render(gain, testsignals::sine(1000.0, -12.0, 48000), 128);
    CHECK_NEAR(testsignals::peakDb(output), -18.0, 0.01);
}

// A plugin that keeps state wrongly across blocks fails here. Do this for every plugin.
TEST(processor, block_size_does_not_change_the_result)
{
    const auto input = testsignals::sine(440.0, -12.0, 20000);
    TestGain gain;
    gain.gainDb().set(-3.0f);

    const auto reference = testsignals::render(gain, input, 8192);
    for (int blockSize : {1, 64, 128, 500}) {
        const auto output = testsignals::render(gain, input, blockSize);
        double worst = 0.0;
        for (std::size_t i = 0; i < output.size(); ++i)
            worst = std::max(worst, double(std::abs(output[i] - reference[i])));
        CHECK(worst < 1e-5);
    }
}

TEST(processor, parameters_are_listed_for_the_ui)
{
    TestGain gain;
    CHECK(gain.parameters().size() == 1);
    CHECK(gain.parameters()[0]->id == "gain");
}
