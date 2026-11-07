#pragma once

#include <string>
#include <vector>

// What the GuitarSuite pedal editor shows for each pedal type, and how it labels values.
// Pure C++: the QML editor only lays these out.
namespace pedaleditor {

// Extra sections a pedal card shows besides its dials.
struct Features {
    bool pitchDelayNote = false;  // octaver / pitch shift: "shifted voices have a short delay"
    bool cabinetIr = false;       // load / clear an impulse response
    bool tuner = false;           // tuner display instead of dials
    bool sustainToggle = false;   // tremolo "Sustain only"
    bool tempoSync = false;       // tremolo / phaser: sync to BPM + beat division
};
Features features(const std::string& pedalType);

// Parameters shown as dials (sync/division/sustain have their own controls; the tuner has none).
bool isDial(const std::string& pedalType, const std::string& parameterId);
// Dials disabled while the pedal is synced to BPM (the free Rate).
bool disabledWhenSynced(const std::string& pedalType, const std::string& parameterId);
// Dial step: whole semitones for pitch shift, whole channels for the amp, otherwise continuous (0).
double dialStep(const std::string& pedalType, const std::string& parameterId);
// Dial value text: no decimals for ms and semitones ("st"), two otherwise, then the unit.
// Unit "channel" (the amp's switch) shows the channel name: Clean, Crunch, High Gain.
std::string formatValue(double value, const std::string& unit);
// Input trim readout with a sign: "+1.5 dB", "0.0 dB", "-3.0 dB".
std::string formatTrim(double db);

// Beat divisions offered by tempo sync, in tempo::noteFrequency order.
std::vector<std::string> beatDivisionNames();
// "120 BPM · follows bottom-bar tempo" when synced, otherwise the free-rate hint.
std::string syncText(bool synced, double bpm);

// Tuner: string buttons ("6 · E2" ... "1 · E4"), reference caption, and readout texts.
std::vector<std::string> tunerStringLabels();
std::string tunerReferenceText();
inline constexpr double inTuneCents = 3.0;
bool inTune(double cents) noexcept;
std::string tunerStatus(bool hasPitch, bool bypassed, double cents);
std::string formatCents(double cents);                       // "+2.5 cents"
std::string formatTunerFrequency(double hz, double targetHz); // "110.2 Hz → 110.00 Hz"
}
