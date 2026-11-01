#include "testkit.h"

#include "processor/smoother.h"

TEST(smoother, reaches_the_target_without_jumping)
{
    Smoother smoother;
    smoother.prepare(48000.0, 20.0f);
    smoother.reset(0.0f);

    CHECK(smoother.next(1.0f) < 0.01f);                    // no jump on the first sample
    float value = 0.0f;
    for (int i = 0; i < 48000; ++i) value = smoother.next(1.0f);
    CHECK_NEAR(value, 1.0, 1e-4);
}
