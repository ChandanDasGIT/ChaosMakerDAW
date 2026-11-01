#pragma once
#include "processor/processor.h"
#include "pitchdetector.h"
#include <atomic>
class Tuner final : public Processor {
public:
    Tuner() : targetString(addParameter("string", "Target string", 0, 6, 0)) {}
    int selectedString() const noexcept { return int(targetString.get() + 0.5f); }
    std::string typeId() const override { return "pedal.tuner"; }
    bool analysisOnly() const noexcept override { return true; }
    void prepare(double sampleRate, int) override { detector.prepare(sampleRate); reset(); }
    void reset() noexcept override { detector.reset(); published.store(0, std::memory_order_relaxed); }
    void process(ProcessContext& context) noexcept override;
    unsigned revision() const noexcept { return updates.load(std::memory_order_relaxed); }
    float frequency() const noexcept { return published.load(std::memory_order_relaxed); }
private:
    Parameter& targetString; // 0 = automatic, 1..6 = low E through high E
    PitchDetector detector;
    std::atomic<float> published{0};
    std::atomic<unsigned> updates{0};
};
