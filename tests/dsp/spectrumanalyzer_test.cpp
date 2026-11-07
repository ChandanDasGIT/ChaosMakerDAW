#include "testkit.h"
#include "testsignals.h"
#include "dsp/spectrumanalyzer.h"

#include <cmath>
#include <vector>

namespace {
std::vector<double> displayFrequencies()
{
    std::vector<double> hz;
    for (int i = 0; i < 200; ++i) hz.push_back(20.0 * std::pow(1000.0, i / 199.0));
    return hz;
}

double levelNear(const std::vector<double>& hz, const std::vector<double>& db, double target)
{
    std::size_t best = 0;
    for (std::size_t i = 1; i < hz.size(); ++i)
        if (std::abs(std::log(hz[i] / target)) < std::abs(std::log(hz[best] / target))) best = i;
    return db[best];
}
}

TEST(spectrumanalyzer, full_scale_sine_reads_about_0_db_at_its_frequency)
{
    SpectrumAnalyzer analyzer(4096);
    analyzer.setSampleRate(testsignals::kSampleRate);
    analyzer.push(testsignals::sine(1000.0, 0.0, 8192));
    const auto hz = displayFrequencies();
    const auto db = analyzer.update(hz, 0.0);
    CHECK_NEAR(levelNear(hz, db, 1000.0), 0.0, 1.5);   // Hann scalloping is at most 1.42 dB
    CHECK(levelNear(hz, db, 100.0) < -60.0);
    CHECK(levelNear(hz, db, 10000.0) < -60.0);

    // -20 dB input reads 20 dB lower.
    SpectrumAnalyzer quieter(4096);
    quieter.push(testsignals::sine(1000.0, -20.0, 8192));
    const auto low = quieter.update(hz, 0.0);
    CHECK_NEAR(levelNear(hz, low, 1000.0) - levelNear(hz, db, 1000.0), -20.0, 0.1);
}

TEST(spectrumanalyzer, silence_sits_on_the_floor)
{
    SpectrumAnalyzer analyzer(1024);
    analyzer.push(std::vector<float>(2048, 0.0f));
    const auto hz = displayFrequencies();
    for (double v : analyzer.update(hz, 0.0)) CHECK(v == SpectrumAnalyzer::FloorDb);
}

TEST(spectrumanalyzer, levels_rise_at_once_and_fall_at_the_set_rate)
{
    SpectrumAnalyzer analyzer(2048);
    analyzer.setFallRate(60.0);
    const auto hz = displayFrequencies();
    analyzer.push(testsignals::sine(2000.0, -6.0, 4096));
    const double loud = levelNear(hz, analyzer.update(hz, 0.1), 2000.0);
    CHECK(loud > -10.0);
    analyzer.push(std::vector<float>(4096, 0.0f));
    // 0.1 s at 60 dB/s: 6 dB lower, not straight to the floor.
    CHECK_NEAR(levelNear(hz, analyzer.update(hz, 0.1), 2000.0), loud - 6.0, 1e-9);
    analyzer.clear();
    CHECK(levelNear(hz, analyzer.update(hz, 0.1), 2000.0) == SpectrumAnalyzer::FloorDb);
}
