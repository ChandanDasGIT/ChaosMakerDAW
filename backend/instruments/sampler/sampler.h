#pragma once
#include "instruments/sampler/samplervoice.h"
#include "instruments/sampler/zonemap.h"
#include "processor/processor.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// A sample player: turns notes into voices that play the matching zones.
//
// prepare(), setPolyphony() and setZones() allocate and run off the audio thread, never while
// process() or render() runs. Everything else runs on the audio thread without allocating.
//
//   MIDI           process() plays note-on / note-off at their frame in the block; CC 123 (all
//                  notes off) releases every note, CC 120 (all sound off) silences at once.
//                  The output is added to the buffer: left and right into channels 0 and 1,
//                  their average into a mono buffer.
//   round robins   each key counts its presses; the count picks the round-robin zone
//   release zones  play on note-off, at the velocity of the note-on, and are not cut by it
//   one-shot zones ignore note-off and play to their end (drums)
//   choke groups   a starting zone fades out every voice whose zone has offBy == its group
//                  (a closed hi-hat stops the open one); group 0 chokes nothing
//   polyphony      at most `polyphony` voices sound unfaded; one more fades out the oldest.
//                  The pool holds twice that, so fading voices finish without a click.
class Sampler final : public Processor {
public:
    static constexpr int maxZonesPerNote = 32;             // layers (mics, release) one note may start

    Sampler();

    std::string typeId() const override { return "sampler"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;                        // silences at once, clears the counters
    void process(ProcessContext& context) noexcept override;

    void setPolyphony(int polyphony);                      // default 64; stops every voice

    // Stops every voice, then replaces the zones. Returns how many zones were dropped.
    int setZones(std::vector<SampleZone> zones);

    void noteOn(int key, int velocity) noexcept;           // velocity 0 is a note-off
    void noteOff(int key) noexcept;
    void allNotesOff() noexcept;                           // releases every held note
    void allSoundOff() noexcept;                           // silences at once, keeps the counters

    // Adds `frames` frames of every voice to left and right.
    void render(float* left, float* right, int frames) noexcept;

    int activeVoices() const noexcept;
    std::span<const SamplerVoice> voices() const noexcept { return m_voices; }
    const ZoneMap& zones() const noexcept { return m_zones; }

private:
    void handle(const MidiEvent& event) noexcept;
    void renderInto(const AudioView& audio, int start, int end) noexcept;
    SamplerVoice* freeVoice() noexcept;
    void limitPolyphony() noexcept;
    void startZones(int key, int velocity, std::span<const int> zones) noexcept;

    ZoneMap m_zones;
    double m_sampleRate = 48000.0;
    int m_polyphony = 64;
    std::vector<SamplerVoice> m_voices;
    std::vector<std::uint64_t> m_started;                  // when each voice started, for stealing
    std::uint64_t m_clock = 0;
    std::array<std::uint32_t, 128> m_presses{};            // round-robin counts, note-on
    std::array<std::uint32_t, 128> m_releases{};           // round-robin counts, release zones
    std::array<int, 128> m_velocity{};                     // last note-on velocity of each key
    std::array<bool, 128> m_held{};
    std::vector<float> m_left, m_right;                    // one block of the mix, before the output
};
