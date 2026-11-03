#include "testkit.h"
#include "plugins/eq/tenbandeq.h"

TEST(tenbandeq, band_descriptors_and_frequencies) {
    const auto& descriptors = TenBandEq::bandDescriptors();
    CHECK(descriptors.size() == 10);

    const std::vector<std::string> expectedLabels = {
        "31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"
    };

    for (int i = 0; i < 10; ++i) {
        CHECK(descriptors[i].index == i);
        CHECK(descriptors[i].shortLabel == expectedLabels[i]);
        CHECK(descriptors[i].minGainDb == -12.0f);
        CHECK(descriptors[i].maxGainDb == 12.0f);
        CHECK(descriptors[i].stepSizeDb == 0.5f);
        CHECK(descriptors[i].defaultGainDb == 0.0f);
        CHECK(descriptors[i].frequencyHz == TenBandEq::frequencies()[i]);
    }

    CHECK(TenBandEq::bandDescriptor(0).shortLabel == "31");
    CHECK(TenBandEq::bandDescriptor(9).shortLabel == "16k");
    CHECK(TenBandEq::bandDescriptor(-1).index == 0); // fallback
}

TEST(tenbandeq, gain_validation_clamping_and_formatting) {
    CHECK(TenBandEq::isValidGain(0.0f));
    CHECK(TenBandEq::isValidGain(-12.0f));
    CHECK(TenBandEq::isValidGain(12.0f));
    CHECK(!TenBandEq::isValidGain(-12.1f));
    CHECK(!TenBandEq::isValidGain(12.1f));

    CHECK(TenBandEq::clampGain(-20.0f) == -12.0f);
    CHECK(TenBandEq::clampGain(20.0f) == 12.0f);
    CHECK(TenBandEq::clampGain(3.5f) == 3.5f);

    CHECK(TenBandEq::formatGain(0.0f) == "0.0 dB");
    CHECK(TenBandEq::formatGain(-6.5f) == "-6.5 dB");
    CHECK(TenBandEq::formatGain(12.0f) == "12.0 dB");
}

TEST(tenbandeq, set_and_get_band_gain) {
    TenBandEq eq;
    CHECK(eq.bandGain(0) == 0.0f);

    eq.setBandGain(0, 4.5f);
    CHECK(eq.bandGain(0) == 4.5f);

    // Clamped
    eq.setBandGain(0, 15.0f);
    CHECK(eq.bandGain(0) == 12.0f);
}
