#pragma once

namespace levelscale {
inline constexpr double silenceDb = -120.0;
inline constexpr double meterFloorDb = -60.0;
inline constexpr double warningDb = -12.0;
inline constexpr double decayDbPerSecond = 24.0;

enum class Band { Normal, Warning, Clip };

// Finite peak/gain conversion. Nonpositive/nonfinite gain is silence; -120 dB is the floor.
double gainToDb(double gain) noexcept;
// Nonfinite dB yields zero; finite overflow saturates to the largest finite gain.
double dbToGain(double db) noexcept;
// Presentation values only; realtime clip latching stays in PeakMeter.
double normalizedLevel(double db) noexcept;
Band classify(double db) noexcept;
// Peak hold/decay evaluated off-thread. Invalid elapsed time applies no decay.
double decayedPeakDb(double previousDb, double peakGain, double elapsedSeconds) noexcept;
}
