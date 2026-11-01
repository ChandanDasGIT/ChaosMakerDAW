#include "testkit.h"
#include "testsignals.h"

#include "plugins/exampleeffect/exampleeffect.h"

#include <algorithm>
#include <cmath>

// Feed a known signal, measure what comes out.
TEST(exampleeffect, half_amount_lowers_a_sine_by_six_db)
{
    ExampleEffect effect;
    effect.amount().set(0.5f);
    const auto output = testsignals::render(effect, testsignals::sine(1000.0, -12.0, 48000), 128);
    CHECK_NEAR(testsignals::peakDb(output), -18.0206, 0.01);
}

// Keep this test for every plugin. It catches state that goes wrong across blocks.
TEST(exampleeffect, block_size_does_not_change_the_result)
{
    const auto input = testsignals::sine(440.0, -12.0, 20000);
    ExampleEffect effect;
    const auto reference = testsignals::render(effect, input, 8192);

    for (int blockSize : {1, 64, 128, 500}) {
        const auto output = testsignals::render(effect, input, blockSize);
        double worst = 0.0;
        for (std::size_t i = 0; i < output.size(); ++i)
            worst = std::max(worst, double(std::abs(output[i] - reference[i])));
        CHECK(worst < 1e-5);
    }
}
