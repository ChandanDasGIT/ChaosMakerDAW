#include "testkit.h"
#include "dsp/biquad.h"
#include <cmath>

TEST(biquad, default_section_passes_signal_unchanged)
{
    const biquad::Coefficients through;
    biquad::State state;
    for (double in : {1.0, -0.5, 0.25, 0.0, 0.75})
        CHECK(biquad::process(through, state, in) == in);
    CHECK_NEAR(biquad::magnitude(through, 100.0, 48000.0), 1.0, 1e-12);
    CHECK_NEAR(biquad::magnitude(through, 20000.0, 48000.0), 1.0, 1e-12);
}

TEST(biquad, two_point_average_passes_dc_and_removes_nyquist)
{
    // y[n] = (x[n] + x[n-1]) / 2
    const biquad::Coefficients average{0.5, 0.5, 0.0, 0.0, 0.0};
    CHECK_NEAR(biquad::magnitude(average, 0.0, 48000.0), 1.0, 1e-12);
    CHECK_NEAR(biquad::magnitude(average, 24000.0, 48000.0), 0.0, 1e-12);
    CHECK_NEAR(biquad::magnitude(average, 12000.0, 48000.0), std::sqrt(0.5), 1e-12);

    biquad::State state;
    CHECK_NEAR(biquad::process(average, state, 1.0), 0.5, 1e-12);
    CHECK_NEAR(biquad::process(average, state, 1.0), 1.0, 1e-12);
    CHECK_NEAR(biquad::process(average, state, -1.0), 0.0, 1e-12);
}

TEST(biquad, feedback_section_rings_down_as_expected)
{
    // y[n] = x[n] + 0.5 y[n-1]: an impulse gives 1, 0.5, 0.25, ...
    const biquad::Coefficients decay{1.0, 0.0, 0.0, -0.5, 0.0};
    biquad::State state;
    double expected = 1.0;
    for (int i = 0; i < 8; ++i) {
        CHECK_NEAR(biquad::process(decay, state, i == 0 ? 1.0 : 0.0), expected, 1e-12);
        expected *= 0.5;
    }
    CHECK_NEAR(biquad::magnitude(decay, 0.0, 48000.0), 2.0, 1e-12);   // 1 / (1 - 0.5)
}
