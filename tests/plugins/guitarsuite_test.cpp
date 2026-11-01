#include "testkit.h"
#include "testsignals.h"

#include "plugins/delay/delaypedal.h"
#include "plugins/guitarsuite/guitarsuite.h"
#include "plugins/overdrive/overdrive.h"

#include <cmath>
#include <memory>

namespace {

std::shared_ptr<PedalSlot> slot(int id, const char* type, std::shared_ptr<Processor> processor)
{
    auto result = std::make_shared<PedalSlot>();
    result->id = id;
    result->type = type;
    result->processor = std::move(processor);
    return result;
}

void setParameter(Processor& processor, const char* id, float value)
{
    for (const auto& parameter : processor.parameters()) {
        if (parameter->id == id) {
            parameter->set(value);
            return;
        }
    }
}

}

TEST(guitarsuite, reordering_pedals_changes_the_rendered_signal)
{
    auto overdrive = std::make_shared<Overdrive>();
    auto delay = std::make_shared<DelayPedal>();
    setParameter(*overdrive, "drive", 26.0f);
    setParameter(*overdrive, "level", -4.0f);
    setParameter(*delay, "time", 80.0f);
    setParameter(*delay, "feedback", 0.66f);
    setParameter(*delay, "mix", 0.72f);

    auto driveSlot = slot(1, "pedal.overdrive", overdrive);
    auto delaySlot = slot(2, "pedal.delay", delay);
    GuitarSuite driveThenDelay({driveSlot, delaySlot});
    const auto first = testsignals::render(driveThenDelay, testsignals::sine(220.0, -8.0, 48000), 128);

    GuitarSuite delayThenDrive({delaySlot, driveSlot});
    const auto second = testsignals::render(delayThenDrive, testsignals::sine(220.0, -8.0, 48000), 128);

    double maxDifference = 0.0;
    for (std::size_t i = 0; i < first.size(); ++i)
        maxDifference = std::max(maxDifference, std::abs(double(first[i] - second[i])));
    CHECK(maxDifference > 0.02);
}

TEST(guitarsuite, bypass_fades_the_slot_to_dry_signal)
{
    auto overdrive = std::make_shared<Overdrive>();
    setParameter(*overdrive, "drive", 30.0f);
    setParameter(*overdrive, "level", 0.0f);
    auto driveSlot = slot(1, "pedal.overdrive", overdrive);
    driveSlot->bypassed.store(true);
    GuitarSuite suite({driveSlot});
    const auto output = testsignals::render(suite, testsignals::sine(440.0, -12.0, 2048), 128);
    const auto input = testsignals::sine(440.0, -12.0, 2048);
    double maxDifference = 0.0;
    for (std::size_t i = 32; i < output.size(); ++i)
        maxDifference = std::max(maxDifference, std::abs(double(output[i] - input[i])));
    CHECK(maxDifference < 1e-5);
}
