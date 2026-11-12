#pragma once
#include "instruments/bass/basspickup.h"
#include "instruments/bass/basssettings.h"
#include "instruments/bass/bassstring.h"
#include "processor/processor.h"

#include <array>
#include <vector>

// The bass guitar instrument: four modelled strings (bassstring), all hand-written.
//
//   strings     E1 A1 D2 G2, 24 frets. Keys outside E1..G4 move by octaves into it. Each key
//               goes on a string and fret as a left hand would choose (bassfretboard): near
//               where the hand is, on a string not already holding a key. One note per string:
//               a new note on a string cuts the one ringing there (a quick fade), as on a bass.
//   styles      Finger, Slap (thumb on E / A, pop on D / G) or Mute (palm mute). The page sets
//               it; the keyswitches C0 / D0 / E0 change it while playing (when on) until the
//               page's style changes; "slap on hard hits" makes Finger notes from a velocity up
//               slap or pop.
//   slides      a key pressed while another has been held for a moment slides there on the
//               held key's string, without a new pluck (Settings::slideSeconds; 0 = never).
//   release     a released key damps its string, like a lifted finger.
//   MIDI        notes with velocity, pitch bend (Settings::bendSemitones), CC 64 sustain
//               pedal (released strings ring on), CC 123 all notes off, CC 120 all sound off
//   output      strings -> pickups -> tone -> drive -> compressor, added to the buffer: the same
//               mono sound on channels 0 and 1
//
// setSettings() may be called from any thread (atomic parameters); the audio thread picks the
// values up at the next short block.
class BassGuitar final : public Processor {
public:
    BassGuitar();

    std::string typeId() const override { return "bassguitar"; }
    void prepare(double sampleRate, int maxFrames) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    void setSettings(const bass::Settings& settings) noexcept;
    bass::Settings settings() const noexcept;

    // For tests and the page: what is sounding.
    int activeVoices() const noexcept;
    // The string a held key plays on, or -1.
    int stringOf(int key) const noexcept;
    // The style the next note plays in (after keyswitches).
    bass::Style currentStyle() const noexcept;

private:
    static constexpr int Block = 32;                // frames between control updates
    static constexpr int VoicesPerString = 2;       // the note and the one fading out
    // How long a key must have been held before a new key slides from it (a chord's notes
    // land together and must not slide).
    static constexpr double SlideAfterSeconds = 0.03;

    struct StringState {
        std::array<bass::BassString, VoicesPerString> voices;
        int current = 0;                            // the voice of the latest note
        int key = -1;                               // the key held on this string, or -1
        unsigned long long since = 0;               // when that key went down (m_clock)
        std::int64_t sinceFrame = 0;                // ... in frames
        bool sustained = false;                     // released while the pedal was down
    };

    void readSettings() noexcept;
    void handle(const MidiEvent& event) noexcept;
    void noteOn(int key, int velocity) noexcept;
    void noteOff(int key) noexcept;
    void pedal(bool down) noexcept;
    void allNotesOff() noexcept;
    void allSoundOff() noexcept;
    bool trySlide(int key, int played) noexcept;
    void renderRange(const AudioView& audio, int start, int end) noexcept;
    void renderBlock(float* mono, int frames) noexcept;
    bass::StringControls stringControls() const noexcept;

    std::vector<Parameter*> m_params;               // one per field, in the field table's order

    double m_sampleRate = 48000.0;
    bass::Settings m_s;                             // as the audio thread last read them
    std::array<StringState, bass::StringCount> m_strings{};
    std::array<int, 128> m_keyString{};             // held key -> string, -1 when none
    unsigned long long m_clock = 0;
    std::int64_t m_frame = 0;                       // frames rendered since reset
    double m_handFret = 5.0;
    int m_switched = -1;                            // a keyswitch's style, -1 = the page's
    bass::Style m_pageStyle = bass::Style::Finger;
    bool m_pedal = false;
    double m_bend = 0.0;                            // -1..1
    bass::ToneStage m_tone;
};
