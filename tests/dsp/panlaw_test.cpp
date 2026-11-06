#include "testkit.h"
#include "dsp/panlaw.h"
#include <cmath>
#include <limits>

TEST(panlaw, centre_is_unity_and_hard_pan_silences_the_other_side)
{
    const auto centre = panlaw::balance(0.0);
    CHECK(centre.left == 1.0f && centre.right == 1.0f);
    const auto left = panlaw::balance(-1.0);
    CHECK(left.left == 1.0f && left.right == 0.0f);
    const auto right = panlaw::balance(1.0);
    CHECK(right.left == 0.0f && right.right == 1.0f);
}

TEST(panlaw, partial_pan_turns_down_only_the_far_side_on_a_sine_curve)
{
    const auto half = panlaw::balance(0.5);
    CHECK(half.right == 1.0f);
    CHECK_NEAR(half.left, std::sqrt(0.5), 1e-6);                 // -3 dB halfway
    const auto mirrored = panlaw::balance(-0.5);
    CHECK(mirrored.left == 1.0f);
    CHECK_NEAR(mirrored.right, half.left, 1e-9);
    // Never louder than unity, and the far side only falls as the pan grows.
    float previous = 1.0f;
    for (int i = 0; i <= 20; ++i) {
        const auto g = panlaw::balance(i / 20.0);
        CHECK(g.left <= previous && g.left >= 0.0f && g.right == 1.0f);
        previous = g.left;
    }
}

TEST(panlaw, clamps_out_of_range_and_non_finite_values)
{
    CHECK(panlaw::clampPan(3.0) == 1.0 && panlaw::clampPan(-3.0) == -1.0);
    CHECK(panlaw::clampPan(std::numeric_limits<double>::quiet_NaN()) == 0.0);
    CHECK(panlaw::clampPan(std::numeric_limits<double>::infinity()) == 0.0);
    const auto nan = panlaw::balance(std::numeric_limits<double>::quiet_NaN());
    CHECK(nan.left == 1.0f && nan.right == 1.0f);
    CHECK(panlaw::balance(5.0).left == 0.0f);
}
