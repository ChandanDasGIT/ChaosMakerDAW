#pragma once
#include "dsp/biquad.h"
#include "instruments/harmonium/harmoniumsettings.h"
#include "instruments/harmonium/reedvoice.h"
#include "processor/processor.h"

#include <array>
#include <cstdint>

// The harmonium instrument: reeds, bellows and a wooden case, all synthesised.
//
//   keys        note-on opens a key's valve, note-off shuts it; velocity is ignored (a harmonium
//               has none). A key pressed again while it sounds keeps sounding.
//   bellows     Settings::bellows times MIDI CC 11 (expression) times CC 2 (breath) is the air
//               pressure. It is smoothed, dips with each pump stroke (Settings::pumping) and
//               sags a little when many reeds play. More air: louder, brighter, a little flat.
//   stops       open and close their reed set on held keys too, in about 15 ms
//   drones      up to four keys that sound until the drone is turned off, through a valve
//               only partly open: softer and darker than a played key
//   sustain     a released key keeps ringing (Settings::sustainSeconds) unless the player has
//               already moved on to a newer key; the next key pressed cuts the tail
//   transpose   the scale changer: moves every key, drones included
//   output      added to the buffer: channels 0 and 1 get the stereo mix, a mono buffer the sum
//
// setSettings() may be called from any thread (atomic parameters); the audio thread picks the
// values up at the next short block. The constructor builds the shared reed tables.
class Harmonium final : public Processor {
public:
    static constexpr int Polyphony = 32;                    // keys at once, before the oldest fades

    Harmonium();

    std::string typeId() const override { return "harmonium"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    void setSettings(const harmonium::Settings& settings) noexcept;
    harmonium::Settings settings() const noexcept;

    int activeVoices() const noexcept;                      // keys and drones
    float pressure() const noexcept { return m_pressure; }  // smoothed air, 0..1

private:
    static constexpr int Block = 32;                        // frames between control updates
    static constexpr int PoolSize = Polyphony + 16;
    static constexpr int MaxDelay = 2048;                   // reflections, up to 10 ms at 192 kHz
    static constexpr float DroneAir = 0.55f;                // share of the bellows a drone gets

    void handle(const MidiEvent& event) noexcept;
    void noteOn(int key) noexcept;
    void noteOff(int key) noexcept;
    bool movedOn(std::size_t voice) const noexcept;
    void allNotesOff() noexcept;
    void allSoundOff() noexcept;
    harmonium::ReedVoice* voiceFor(int key) noexcept;
    harmonium::ReedVoice* freeVoice() noexcept;
    void updateControls() noexcept;                         // once per Block
    void updateDrones() noexcept;
    void designBody() noexcept;
    double attackSeconds(int soundingKey) const noexcept;
    float noise() noexcept;
    void renderBlock(float* left, float* right, int frames) noexcept;
    void renderRange(const AudioView& audio, int start, int end) noexcept;

    struct Params {
        Parameter *style, *bass, *male, *female, *bourdon16, *diapason8, *dulciana8, *principal4,
                  *voxHumana, *bellows, *pumping, *beating, *airNoise, *keyClick, *caseResonance,
                  *transpose, *volume, *sustain;
        std::array<Parameter*, harmonium::DroneCount> drones;
    } m_p{};

    double m_sampleRate = 48000.0;
    std::array<harmonium::ReedVoice, PoolSize> m_voices{};
    std::array<std::uint64_t, PoolSize> m_started{};
    std::array<std::uint64_t, PoolSize> m_startFrame{};      // when each key went down
    std::uint64_t m_now = 0;                                // frames rendered since reset
    std::array<harmonium::ReedVoice, harmonium::DroneCount> m_drones{};
    std::uint64_t m_clock = 0;

    // Controls as the audio thread last applied them.
    harmonium::Style m_style = harmonium::Style::Indian;
    std::array<harmonium::Rank, harmonium::MaxRanks> m_ranks{};
    float m_caseResonance = -1.0f;
    float m_expression = 1.0f, m_breath = 1.0f;             // CC 11, CC 2
    float m_pressure = 0.0f, m_pressureTarget = 0.0f;
    float m_pressureCoef = 0.0f;
    double m_pumpPhase = 0.0, m_pumpRate = 1.1, m_tremoloPhase = 0.0;
    float m_click = 0.0f, m_clickCoef = 0.0f;
    float m_gain = 1.0f;
    std::uint32_t m_seed = 0x1234567u;

    // The case: a few EQ sections on the mono reed sound, then short wooden reflections.
    static constexpr int BodySections = 4;
    std::array<biquad::Coefficients, BodySections> m_body{};
    std::array<biquad::State, BodySections> m_bodyState{};
    biquad::Coefficients m_airFilter{}, m_clickFilter{};
    biquad::State m_airState{}, m_clickState{};
    std::array<float, MaxDelay> m_delay{};
    int m_delayWrite = 0;
    std::array<int, 4> m_tapFrames{};                       // two taps per side
    float m_reflection = 0.0f;
    biquad::Coefficients m_reflectionFilter{};              // wood takes the highs out of them
    std::array<biquad::State, 2> m_reflectionState{};
};
