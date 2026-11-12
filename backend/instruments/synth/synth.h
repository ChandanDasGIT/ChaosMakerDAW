#pragma once
#include "instruments/synth/synthlfo.h"
#include "instruments/synth/synthsettings.h"
#include "instruments/synth/synthvoice.h"
#include "processor/processor.h"

#include <array>
#include <cstdint>
#include <vector>

// The subtractive synth instrument, all hand-written.
//
//   voices      Poly: up to Polyphony keys at once; past that the oldest held key fades out in
//               a few ms. A key played again reuses its own voice (no pile-up of tails).
//               Mono / Legato: one voice; releasing the newest key goes back to the one still
//               held. Mono restarts the envelopes on every key, Legato only when no other key
//               is held. Glide slides the pitch in both (Legato: only between held keys).
//   LFO         one for the whole synth. Free rate in Hz, or synced to the project tempo by a
//               note division (tempo::noteFrequency, like the guitar tremolo); while the song
//               plays a synced LFO is locked to the song's beat, so it lines up with the bars.
//   MIDI        notes with velocity, pitch bend (Settings::bendSemitones), CC 1 mod wheel
//               (adds to the LFO depth), CC 64 sustain pedal, CC 123 all notes off, CC 120
//               all sound off
//   output      added to the buffer: the same mono sound on channels 0 and 1
//
// setSettings() may be called from any thread (atomic parameters); the audio thread picks the
// values up at the next short block.
class Synth final : public Processor {
public:
    static constexpr int Polyphony = 16;

    Synth();

    std::string typeId() const override { return "synth"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    void setSettings(const synth::Settings& settings) noexcept;
    synth::Settings settings() const noexcept;

    int activeVoices() const noexcept;
    double lfoPhase() const noexcept { return m_lfo.phase(); }

private:
    static constexpr int Block = 32;                // frames between control updates
    static constexpr int PoolSize = Polyphony + 8;  // room for fading voices
    static constexpr int StackSize = 32;            // held keys remembered in Mono / Legato

    void readSettings() noexcept;
    void handle(const MidiEvent& event) noexcept;
    void noteOn(int key, int velocity) noexcept;
    void noteOff(int key) noexcept;
    void pedal(bool down) noexcept;
    void allNotesOff() noexcept;
    void allSoundOff() noexcept;
    bool monophonic() const noexcept { return m_s.mode != synth::PlayMode::Poly; }
    synth::Voice* freeVoice() noexcept;
    void pushKey(int key) noexcept;
    void removeKey(int key) noexcept;
    void renderRange(const AudioView& audio, int start, int end) noexcept;
    void renderBlock(float* mono, int frames) noexcept;

    std::vector<Parameter*> m_params;               // one per field, in the field table's order

    double m_sampleRate = 48000.0;
    synth::Settings m_s;                            // as the audio thread last read them
    std::array<synth::Voice, PoolSize> m_voices{};
    std::array<std::uint64_t, PoolSize> m_started{};
    std::array<bool, PoolSize> m_sustained{};       // released while the pedal was down
    std::uint64_t m_clock = 0;

    std::array<bool, 128> m_down{};                 // keys physically held
    std::array<int, StackSize> m_stack{};           // Mono / Legato: held keys, newest last
    std::array<int, 128> m_velocity{};
    int m_stackSize = 0;
    bool m_monoSustained = false;

    bool m_pedal = false;
    double m_bend = 0.0;                            // -1..1
    double m_modWheel = 0.0;                        // 0..1
    synth::LowFrequencyOscillator m_lfo;

    // The transport, for a synced LFO.
    double m_bpm = 120.0, m_beat = 0.0;
    bool m_playing = false;
    int m_bufferFrame = 0;                          // where in the host buffer this block starts
};
