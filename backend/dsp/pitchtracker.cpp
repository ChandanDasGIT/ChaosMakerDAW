#include "dsp/pitchtracker.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pitch {

double midiOf(double hz) noexcept { return hz > 0.0 ? 69.0 + 12.0 * std::log2(hz / 440.0) : 0.0; }

void Tracker::prepare(double sampleRate)
{
    sampleRate = std::max(sampleRate, 4000.0);
    m_decimation = std::max(1, int(sampleRate / 16000.0));      // at least 16 kHz after it
    m_rate = sampleRate / m_decimation;
    // A Butterworth low-pass section (Q 1/sqrt 2), twice: 4 poles at 40 % of the new rate.
    const double w = 2.0 * std::numbers::pi * std::min(0.4 * m_rate, 0.45 * sampleRate) / sampleRate;
    const double alpha = std::sin(w) / std::numbers::sqrt2;     // sin w / (2 Q), Q = 1 / sqrt 2
    const double cosw = std::cos(w), a0 = 1.0 + alpha;
    m_lowPass = {(1.0 - cosw) / 2.0 / a0, (1.0 - cosw) / a0, (1.0 - cosw) / 2.0 / a0, -2.0 * cosw / a0, (1.0 - alpha) / a0};
    m_maxLag = int(std::ceil(m_rate / MinHz));
    m_minLag = std::max(2, int(std::floor(m_rate / MaxHz)));
    m_window = m_maxLag;
    m_hop = std::max(1, int(std::lround(HopSeconds * m_rate)));
    m_buffer.assign(std::size_t(m_window + m_maxLag + 2), 0.0f);
    m_frame.assign(m_buffer.size(), 0.0f);
    m_difference.assign(std::size_t(m_maxLag + 2), 1.0f);
    m_raw.assign(std::size_t(m_maxLag + 2), 0.0);
    reset();
}

void Tracker::reset() noexcept
{
    m_state = {};
    m_phase = 0;
    m_write = 0;
    m_decimated = 0;
    m_sinceHop = 0;
    std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
}

Frame Tracker::analyseFrame() noexcept
{
    const int size = int(m_buffer.size());
    for (int i = 0; i < size; ++i) m_frame[std::size_t(i)] = m_buffer[std::size_t((m_write + i) % size)];
    Frame frame;
    frame.sample = (m_decimated - size / 2) * m_decimation;
    double energy = 0.0;
    for (int j = 0; j < m_window; ++j) energy += double(m_frame[std::size_t(j)]) * m_frame[std::size_t(j)];
    const double rms = std::sqrt(energy / m_window);
    frame.levelDb = float(20.0 * std::log10(rms + 1e-9));
    if (frame.levelDb < MinLevelDb) return frame;

    // YIN: d(tau) normalised by its running mean.
    const float* x = m_frame.data();
    double cumulative = 0.0;
    m_difference[0] = 1.0f;
    for (int tau = 1; tau <= m_maxLag; ++tau) {
        double sum = 0.0;
        for (int j = 0; j < m_window; ++j) {
            const double d = double(x[j]) - x[j + tau];
            sum += d * d;
        }
        cumulative += sum;
        m_raw[std::size_t(tau)] = sum;
        m_difference[std::size_t(tau)] = cumulative > 1e-20 ? float(sum * tau / cumulative) : 1.0f;
    }
    int best = -1;
    for (int tau = m_minLag; tau < m_maxLag; ++tau) {
        if (m_difference[std::size_t(tau)] >= Threshold) continue;
        while (tau + 1 < m_maxLag && m_difference[std::size_t(tau + 1)] < m_difference[std::size_t(tau)]) ++tau;
        best = tau;
        break;
    }
    if (best < 0) return frame;
    // The parabola goes through the raw difference (the normalised one leans to longer lags).
    const double a = m_raw[std::size_t(best - 1)], b = m_raw[std::size_t(best)], c = m_raw[std::size_t(best + 1)];
    const double denominator = a - 2.0 * b + c;
    const double shift = std::abs(denominator) > 1e-20 ? std::clamp(0.5 * (a - c) / denominator, -1.0, 1.0) : 0.0;
    frame.hz = float(m_rate / (best + shift));
    frame.clarity = std::clamp(1.0f - m_difference[std::size_t(best)], 0.0f, 1.0f);
    return frame;
}

void Tracker::cleanUp(std::vector<Frame>& frames)
{
    // Octave slips: a frame an octave from the median of its voiced neighbours moves back.
    std::vector<double> midi(frames.size(), 0.0);
    for (std::size_t i = 0; i < frames.size(); ++i) midi[i] = frames[i].hz > 0.0f ? midiOf(frames[i].hz) : 0.0;
    std::vector<double> around;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (midi[i] <= 0.0) continue;
        around.clear();
        for (std::size_t j = i >= 5 ? i - 5 : 0; j < std::min(frames.size(), i + 6); ++j)
            if (j != i && midi[j] > 0.0) around.push_back(midi[j]);
        if (around.size() < 4) continue;
        std::nth_element(around.begin(), around.begin() + std::ptrdiff_t(around.size() / 2), around.end());
        const double median = around[around.size() / 2];
        for (const double octave : {12.0, -12.0})
            if (std::abs(midi[i] - octave - median) < 1.5) frames[i].hz = float(frames[i].hz / std::pow(2.0, octave / 12.0));
    }
    // Voiced runs shorter than 3 frames are noise, a consonant or a click.
    for (std::size_t i = 0; i < frames.size();) {
        if (frames[i].hz <= 0.0f) { ++i; continue; }
        std::size_t end = i;
        while (end < frames.size() && frames[end].hz > 0.0f) ++end;
        if (end - i < 3)
            for (std::size_t j = i; j < end; ++j) { frames[j].hz = 0.0f; frames[j].clarity = 0.0f; }
        i = end;
    }
}

std::vector<Frame> Tracker::analyse(std::span<const float> mono, double sampleRate, const std::atomic<bool>* cancel)
{
    std::vector<Frame> frames;
    if (mono.empty() || !(sampleRate > 0.0)) return frames;
    Tracker tracker;
    tracker.prepare(sampleRate);
    frames.reserve(std::size_t(double(mono.size()) / sampleRate / HopSeconds) + 4);
    const auto chunk = std::max<std::size_t>(1, std::size_t(sampleRate));
    for (std::size_t at = 0; at < mono.size(); at += chunk) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return {};
        tracker.push(mono.data() + at, int(std::min(chunk, mono.size() - at)), [&frames](const Frame& f) { frames.push_back(f); });
    }
    // Push silence so the last sound reaches a frame's centre.
    const std::vector<float> tail(std::size_t(tracker.m_buffer.size()) * std::size_t(tracker.m_decimation), 0.0f);
    const auto end = std::int64_t(mono.size());
    tracker.push(tail.data(), int(tail.size()), [&frames, end](const Frame& f) { if (f.sample < end) frames.push_back(f); });
    cleanUp(frames);
    return frames;
}

} // namespace pitch
