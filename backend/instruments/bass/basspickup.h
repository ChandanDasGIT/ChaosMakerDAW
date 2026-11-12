#pragma once

// The bass's electronics.
//
// Pickups: a magnetic pickup hears the string where it sits. A pickup at a fraction b of the
// string's length from the bridge cancels every overtone with a node there; the string reads
// as s(t) - s(t - b * period) (a comb). The neck pickup sits further from the bridge than the
// bridge pickup, so it is rounder and louder in the lows; fretting shortens the string, so the
// same pickup sits at a larger fraction higher up the neck. Each comb's level is evened out
// at the fundamental, so the blend changes the tone more than the volume.
//
// Then the amp side, on the mixed signal: the tone knob (a low-pass), drive (soft clipping)
// and a compressor that evens the notes out.
namespace bass {

// For an open string, the pickups' fractions of its length from the bridge.
constexpr double NeckPickupAt = 0.19;
constexpr double BridgePickupAt = 0.055;

struct PickupTap {
    double delay = 0.0;                            // in samples: the comb's tap
    double gain = 0.0;                             // the blend and the level evening, together
};

// The two taps for a string of `period` samples at `fret` (fractional while sliding), and the
// blend 0 (neck) .. 1 (bridge).
void pickupTaps(double period, double fret, double blend, PickupTap& neck, PickupTap& bridge) noexcept;

// Tone, drive and compressor, one mono signal. Not thread-safe: the audio thread owns it.
class ToneStage {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    // tone, drive, compression 0..1 (Settings).
    void set(double tone, double drive, double compression) noexcept;
    float process(float x) noexcept;

private:
    double m_rate = 48000.0;
    double m_toneCoef = 1.0, m_tone1 = 0.0, m_tone2 = 0.0;
    double m_drive = 0.0, m_preGain = 1.0, m_postGain = 1.0;
    double m_compression = 0.0, m_threshold = 0.0, m_slope = 0.0, m_makeup = 1.0;
    double m_attack = 0.0, m_release = 0.0, m_envelope = 0.0;
};

} // namespace bass
