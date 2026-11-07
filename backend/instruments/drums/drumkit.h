#pragma once
#include "instruments/drums/drumkitlibrary.h"
#include "instruments/drums/drumkitsettings.h"
#include "processor/processor.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

class Sampler;

// The acoustic drum kit instrument (Gogodze Phu Vol II) on the drum map's keys.
//
// It is silent until its library has built the kit's zones (about a second the first time,
// at once for later tracks: the samples are shared), then plays them. setSettings() runs on
// the control thread and has the zones rebuilt in the background; the playing sampler takes
// them at a block boundary (Sampler::stageZones), so nothing on the audio thread waits.
class DrumKit final : public Processor {
public:
    static constexpr int Polyphony = 96;                   // several mics per hit, long hats

    explicit DrumKit(std::shared_ptr<drumkit::Library> library = drumkit::Library::shared());
    ~DrumKit() override;

    std::string typeId() const override { return "drumkit"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    void setSettings(const drumkit::Settings& settings);
    drumkit::Settings settings() const;

    // Hits the kit has played on `key` (any source: playback, MIDI, KEYS, clicks), for the
    // kit picture's lights: the count only grows; velocity is the latest hit's. Any thread.
    struct Hits {
        std::uint32_t count = 0;
        int velocity = 0;
    };
    Hits hits(int key) const noexcept;

    const Sampler& sampler() const noexcept { return *m_sampler; }
    const std::shared_ptr<drumkit::Library>& library() const noexcept { return m_library; }

private:
    std::shared_ptr<Sampler> m_sampler;
    std::shared_ptr<drumkit::Library> m_library;
    mutable std::mutex m_mutex;
    drumkit::Settings m_settings;
    std::array<std::atomic<std::uint32_t>, 128> m_hitCount{};
    std::array<std::atomic<std::uint8_t>, 128> m_hitVelocity{};
};
