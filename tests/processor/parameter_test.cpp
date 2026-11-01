#include "testkit.h"

#include "processor/parameter.h"

TEST(parameter, starts_at_its_default_and_stays_in_range)
{
    Parameter gain("gain", "Gain", -60.0f, 12.0f, 0.0f, "dB");
    CHECK_NEAR(gain.get(), 0.0, 1e-6);

    gain.set(100.0f);
    CHECK_NEAR(gain.get(), 12.0, 1e-6);
    gain.set(-100.0f);
    CHECK_NEAR(gain.get(), -60.0, 1e-6);
}
