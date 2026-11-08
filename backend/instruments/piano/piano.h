#pragma once
#include "instruments/piano/pianolibrary.h"
#include "instruments/piano/pianosettings.h"
#include "processor/processor.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

class Sampler;

// The grand piano instrument (Splendid Grand Piano) for Piano tracks.
//
// It is silent until its library has built the zones (a few seconds the first time, at once
// for later tracks: the samples are shared), then plays them through a Sampler with the
// sustain pedal (CC 64) live. setSettings() runs on the control thread and has the zones
// rebuilt in the background; the playing sampler takes them at a block boundary, so nothing
// on the audio thread waits. Notes sounding when settings change fade out quickly.
class Piano final : public Processor {
public:
    static constexpr int Polyphony = 96;                   // a pedalled run, plus resonance layers

    explicit Piano(std::shared_ptr<piano::Library> library = piano::Library::shared());
    ~Piano() override;

    std::string typeId() const override { return "piano"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    void setSettings(const piano::Settings& settings);
    piano::Settings settings() const;

    // For the page's drawn keyboard and pedal, from any thread: which keys are down (from any
    // source: playback, MIDI, KEYS, clicks) with their velocity, and the pedal (CC 64, 0..127).
    // presses(key) only grows, so a key struck twice between two looks still shows.
    bool keyDown(int key) const noexcept;
    int keyVelocity(int key) const noexcept;
    std::uint32_t presses(int key) const noexcept;
    int pedal() const noexcept { return m_pedal.load(std::memory_order_relaxed); }

    const Sampler& sampler() const noexcept { return *m_sampler; }
    const std::shared_ptr<piano::Library>& library() const noexcept { return m_library; }

private:
    std::shared_ptr<Sampler> m_sampler;
    std::shared_ptr<piano::Library> m_library;
    mutable std::mutex m_mutex;
    piano::Settings m_settings;
    std::array<std::atomic<std::uint8_t>, 128> m_down{};   // velocity while held, 0 when up
    std::array<std::atomic<std::uint32_t>, 128> m_presses{};
    std::atomic<int> m_pedal{0};
};
