#pragma once
#include "instruments/electrodrums/electrosettings.h"
#include "instruments/electrodrums/electrovoices.h"
#include "processor/processor.h"

#include <array>
#include <atomic>
#include <cstdint>

// The electronic drum kit instrument: the synthesised drums (electrovoices) on the drum keys
// (electromap). Hits are one-shots: note-off is ignored, so a short note does not cut a drum.
// A new hit on a drum cuts that drum's last hit (as on a drum machine), and a closed or pedal
// hi-hat cuts a ringing open one. A fixed pool of voices: nothing is allocated while playing.
// setSettings() may be called from any thread (atomic parameters); each new hit uses the values
// as they are then (style, its drum's tune / decay / level, the volume).
class ElectroDrums final : public Processor {
public:
    static constexpr int Polyphony = 32;
    static constexpr int Block = 256;                      // renders in blocks of at most this

    ElectroDrums();

    std::string typeId() const override { return "electrodrums"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    void setSettings(const electrodrums::Settings& settings) noexcept;
    electrodrums::Settings settings() const noexcept;

    // Hits the kit has played on `key` (any source: playback, MIDI, KEYS, clicks), for the
    // picture's lights: the count only grows; velocity is the latest hit's. Any thread.
    struct Hits {
        std::uint32_t count = 0;
        int velocity = 0;
    };
    Hits hits(int key) const noexcept;

    int activeVoices() const noexcept;

    // Velocity (1..127) to gain: a gentle curve, full velocity is 1.
    static float velocityGain(int velocity) noexcept;

private:
    void handle(const MidiEvent& event) noexcept;
    void noteOn(int key, int velocity) noexcept;
    void renderRange(const AudioView& audio, int start, int end) noexcept;
    electrodrums::Voice& freeVoice() noexcept;

    std::array<electrodrums::Voice, Polyphony> m_voices{};
    std::array<std::uint64_t, Polyphony> m_started{};      // hit order, to reuse the oldest
    std::uint64_t m_hits = 0;
    std::uint32_t m_seed = 0x9e3779b9u;
    double m_sampleRate = 48000.0;

    struct DrumParams { Parameter *tune, *decay, *level; };
    struct Params {
        Parameter *style, *volume;
        std::array<DrumParams, electrodrums::DrumCount> drums;
    } m_p{};
    std::array<std::atomic<std::uint32_t>, 128> m_hitCount{};
    std::array<std::atomic<std::uint8_t>, 128> m_hitVelocity{};
};
