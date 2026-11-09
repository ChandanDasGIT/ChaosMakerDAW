#include "dsp/latencymeasure.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace latencymeasure {

std::vector<float> makeClick(double sampleRate)
{
    const int length = std::max(16, static_cast<int>(std::lround(sampleRate * 0.0027)));
    const double f0 = 1000.0;
    const double f1 = std::min(8000.0, sampleRate * 0.4);
    const double seconds = length / sampleRate;
    std::vector<float> click(static_cast<std::size_t>(length));
    for (int i = 0; i < length; ++i) {
        const double t = i / sampleRate;
        const double phase = 2.0 * std::numbers::pi * (f0 * t + 0.5 * (f1 - f0) / seconds * t * t);
        const double window = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (length - 1));
        click[static_cast<std::size_t>(i)] = static_cast<float>(0.8 * window * std::sin(phase));
    }
    return click;
}

Result findDelay(const std::vector<float>& click, const std::vector<float>& recorded, std::int64_t maxDelay)
{
    Result result;
    const auto m = static_cast<std::int64_t>(click.size());
    const auto n = static_cast<std::int64_t>(recorded.size());
    if (m == 0 || n < m) return result;
    std::int64_t last = n - m;
    if (maxDelay >= 0) last = std::min(last, maxDelay);

    double clickEnergy = 0.0;
    for (float s : click) clickEnergy += double(s) * s;
    if (clickEnergy <= 0.0) return result;

    // Energy of the recording under the click at each delay (running sum).
    double windowEnergy = 0.0;
    for (std::int64_t i = 0; i < m; ++i) windowEnergy += double(recorded[i]) * recorded[i];

    double best = 0.0, bestSigned = 0.0, bestNorm = 0.0, sumAbs = 0.0;
    std::int64_t bestAt = -1;
    for (std::int64_t d = 0; d <= last; ++d) {
        double c = 0.0;
        for (std::int64_t i = 0; i < m; ++i) c += double(click[i]) * recorded[d + i];
        const double a = std::abs(c);
        sumAbs += a;
        if (a > best) {
            best = a;
            bestSigned = c;
            bestAt = d;
            bestNorm = windowEnergy > 1e-20 ? a / std::sqrt(clickEnergy * windowEnergy) : 0.0;
        }
        if (d + m < n) {
            windowEnergy += double(recorded[d + m]) * recorded[d + m] - double(recorded[d]) * recorded[d];
            windowEnergy = std::max(0.0, windowEnergy);
        }
    }
    if (bestAt < 0) return result;

    const double meanAbs = sumAbs / double(last + 1);
    result.delaySamples = bestAt;
    result.confidence = std::clamp(bestNorm, 0.0, 1.0);
    result.inverted = bestSigned < 0.0;
    // Clear enough: the click's shape matches, and the peak towers over the average.
    result.found = result.confidence >= 0.5 && best > 8.0 * meanAbs;
    return result;
}

}
