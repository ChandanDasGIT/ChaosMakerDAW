#pragma once

#include "dsp/fft.h"
#include "plugins/mixfx/mixeffect.h"
#include "rt/rtaudioring.h"

#include <array>
#include <atomic>
#include <complex>
#include <span>
#include <vector>

// Noise reduction (restMap G13). The sound is cut into overlapping 2048-sample frames (hop 512,
// sqrt-Hann in and out, so frames add back to the input exactly), and each frequency bin is
// turned down by how much of it is the learned noise: gain = 1 - k * noise / level, where k is
// Sensitivity (how far above the noise counts as wanted sound), never below the Reduction
// floor. Gains fall back slowly (Smoothing) and are averaged with their neighbours, which keeps
// the "chirpy" artefacts of plain spectral subtraction down. "Hear removed noise" plays what
// would be taken out. Both channels share one gain, so the stereo image stays.
//
// It always delays its output by Size samples (latencySamples), learned or not, bypassed or
// not, so the engine can read the track that much earlier and keep it in time. Without a
// learned noise it passes the sound through unchanged (just delayed).
class Denoise final : public MixEffect {
public:
    static constexpr int Size = 2048;
    static constexpr int Hop = Size / 4;
    static constexpr int Bins = Size / 2 + 1;
    static_assert(Bins == mixfx::DenoiseBins, "the saved profile has one level per bin");

    Denoise();
    int latencySamples() const noexcept override { return Size; }

    // The noise in a stretch of mono samples (a noise-only part): each bin's average level over
    // the same frames the effect uses. Empty when there are fewer than Size samples.
    static std::vector<float> learnProfile(std::span<const float> mono);
    // For the page's spectrum: bin levels (as learnProfile gives them) at display frequencies,
    // in dBFS (a full-scale sine reads about 0 dB): each frequency shows the loudest bin of its
    // band. The live sound is measured the same way (learnProfile of its last Size samples), so
    // the learned noise and the sound line up.
    static std::vector<double> curveDb(std::span<const float> levels, std::span<const double> frequencies, double sampleRate);

    // The page's spectrum (any thread): while listening, process() copies its input and output
    // (mixed to mono) into rings; read them from another thread.
    void setAnalyzerListening(bool listening) noexcept { m_listening.store(listening, std::memory_order_relaxed); }
    std::size_t readInput(std::span<float> out) noexcept;
    std::size_t readOutput(std::span<float> out) noexcept;

protected:
    void applyExtra(const mixfx::Settings& settings) noexcept override;
    void prepareEffect(double sampleRate, int maxFrames) override;
    void resetEffect() noexcept override;
    void processEffect(ProcessContext& context) noexcept override;

private:
    enum Index { Reduction, Sensitivity, Smoothing, Listen };
    void processFrame(int channels) noexcept;

    Fft m_fft{Size};
    std::array<float, Size> m_window{};                 // sqrt-Hann
    std::array<std::vector<float>, 2> m_input;          // the last Size samples per channel
    std::array<std::vector<float>, 2> m_output;         // overlap-add, Size samples per channel
    std::array<std::vector<std::complex<double>>, 2> m_spectrum;
    std::array<float, Bins> m_gain{};                   // smoothed over time
    std::array<float, Bins> m_level{};
    int m_hopPos = 0;

    // The learned noise, handed from the UI thread without locks (a triple buffer: the writer
    // fills its own slot and swaps it in; the audio thread swaps in the newest when there is
    // one). Each slot ends with a flag: 1 = a profile, 0 = none.
    using Profile = std::array<float, Bins + 1>;
    std::array<Profile, 3> m_profiles{};
    std::atomic<int> m_shared{1};                       // slot index | Fresh
    int m_writing = 2, m_reading = 0;
    static constexpr int Fresh = 4;

    std::atomic<bool> m_listening{false};
    RtAudioRing<16384> m_inRing, m_outRing;
    std::vector<float> m_mono;
};
