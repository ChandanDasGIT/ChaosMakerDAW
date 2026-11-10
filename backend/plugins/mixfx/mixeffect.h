#pragma once

#include "plugins/mixfx/mixfxspec.h"
#include "processor/processor.h"

#include <atomic>
#include <string_view>
#include <vector>

// The base of every mixing plugin (restMap G7). Its parameters come from the plugin's spec, in
// the spec's order, plus "bypass" (0 / 1). Switching bypass crossfades over 20 ms so it never
// clicks; fully bypassed, the effect does no work and starts from silence when switched back on.
class MixEffect : public Processor {
public:
    explicit MixEffect(std::string_view type);

    std::string typeId() const final { return "mix." + std::string(m_spec->type); }
    const mixfx::Spec& spec() const noexcept { return *m_spec; }
    void prepare(double sampleRate, int maxFrames) final;
    void reset() noexcept final;
    void process(ProcessContext& context) noexcept final;

    // Settings into the parameters. Any thread: parameters are atomic.
    void apply(const mixfx::Settings& settings) noexcept;
    Parameter* parameter(std::string_view id) noexcept;
    // Gain reduction in the last block, in dB (>= 0), for a meter; 0 for effects without one.
    float reductionDb() const noexcept { return m_reductionDb.load(std::memory_order_relaxed); }

protected:
    // The spec's controls, by their index in the spec.
    float control(std::size_t index) const noexcept { return m_controls[index]->get(); }
    double sampleRate() const noexcept { return m_sampleRate; }
    void publishReductionDb(float db) noexcept { m_reductionDb.store(db, std::memory_order_relaxed); }

    virtual void prepareEffect(double sampleRate, int maxFrames) = 0;
    virtual void resetEffect() noexcept = 0;
    // Processes the block in place (up to 2 channels), bypass aside.
    virtual void processEffect(ProcessContext& context) noexcept = 0;

private:
    const mixfx::Spec* m_spec;
    std::vector<Parameter*> m_controls;
    Parameter* m_bypass = nullptr;
    std::vector<float> m_dry[2];
    double m_sampleRate = 48000.0;
    float m_wet = 1.0f;          // 1 on, 0 bypassed, in between while crossfading
    float m_wetStep = 0.001f;
    std::atomic<float> m_reductionDb{0.0f};
};
