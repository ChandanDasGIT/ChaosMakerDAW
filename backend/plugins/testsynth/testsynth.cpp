#include "plugins/testsynth/testsynth.h"

#include <algorithm>
#include <numbers>

TestSynth::TestSynth()
{
    prepare(48000.0, 1024);
}

void TestSynth::prepare(double sampleRate, int maxFrames)
{
    (void)maxFrames;
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    // 5ms attack, 50ms release
    m_attackRate = 1.0f / static_cast<float>(std::max(1.0, 0.005 * m_sampleRate));
    m_releaseRate = 1.0f / static_cast<float>(std::max(1.0, 0.050 * m_sampleRate));
}

void TestSynth::reset() noexcept
{
    for (auto& v : m_voices) {
        v.active = false;
        v.key = -1;
        v.targetGain = 0.0f;
        v.currentLevel = 0.0f;
        v.phase = 0.0;
        v.phaseInc = 0.0;
        v.state = Voice::State::Off;
        v.age = 0;
    }
}

TestSynth::Voice& TestSynth::allocateVoice() noexcept
{
    // 1. Inactive voice
    for (auto& v : m_voices) {
        if (!v.active) return v;
    }

    // 2. Voice in release stage with lowest level
    Voice* bestRelease = nullptr;
    for (auto& v : m_voices) {
        if (v.state == Voice::State::Release) {
            if (!bestRelease || v.currentLevel < bestRelease->currentLevel) {
                bestRelease = &v;
            }
        }
    }
    if (bestRelease) return *bestRelease;

    // 3. Oldest voice
    Voice* oldest = &m_voices[0];
    for (auto& v : m_voices) {
        if (v.age < oldest->age) {
            oldest = &v;
        }
    }
    return *oldest;
}

void TestSynth::noteOn(int key, int velocity) noexcept
{
    if (velocity <= 0) {
        noteOff(key);
        return;
    }

    auto& v = allocateVoice();
    v.active = true;
    v.key = key;
    v.targetGain = std::clamp(velocity, 0, 127) / 127.0f;
    v.currentLevel = 0.0f;
    v.phase = 0.0;

    const double freq = 440.0 * std::pow(2.0, (key - 69.0) / 12.0);
    v.phaseInc = freq / m_sampleRate;
    v.state = Voice::State::Attack;
    v.age = ++m_ageCounter;
}

void TestSynth::noteOff(int key) noexcept
{
    for (auto& v : m_voices) {
        if (v.active && v.key == key && v.state != Voice::State::Release && v.state != Voice::State::Off) {
            v.state = Voice::State::Release;
        }
    }
}

void TestSynth::allNotesOff() noexcept
{
    for (auto& v : m_voices) {
        if (v.active) {
            v.state = Voice::State::Release;
        }
    }
}

void TestSynth::process(ProcessContext& context) noexcept
{
    const int totalFrames = context.audio.frames;
    if (totalFrames <= 0 || context.audio.channelCount <= 0) return;

    auto renderVoices = [&](int start, int end) {
        if (start >= end) return;
        constexpr double twoPi = 2.0 * std::numbers::pi;

        for (int frame = start; frame < end; ++frame) {
            float sample = 0.0f;

            for (auto& v : m_voices) {
                if (!v.active) continue;

                if (v.state == Voice::State::Attack) {
                    v.currentLevel += m_attackRate * v.targetGain;
                    if (v.currentLevel >= v.targetGain) {
                        v.currentLevel = v.targetGain;
                        v.state = Voice::State::Sustain;
                    }
                } else if (v.state == Voice::State::Release) {
                    v.currentLevel -= m_releaseRate * v.targetGain;
                    if (v.currentLevel <= 0.0f) {
                        v.currentLevel = 0.0f;
                        v.active = false;
                        v.state = Voice::State::Off;
                        continue;
                    }
                }

                sample += static_cast<float>(std::sin(v.phase * twoPi)) * v.currentLevel * 0.25f;

                v.phase += v.phaseInc;
                if (v.phase >= 1.0) {
                    v.phase -= std::floor(v.phase);
                }
            }

            for (int c = 0; c < context.audio.channelCount; ++c) {
                context.audio.channel(c)[frame] += sample;
            }
        }
    };

    int currentFrame = 0;
    if (context.midi) {
        for (const auto& ev : context.midi->events()) {
            const int target = std::clamp(ev.offset, 0, totalFrames);
            if (target > currentFrame) {
                renderVoices(currentFrame, target);
                currentFrame = target;
            }

            const auto statusType = ev.status & 0xF0;
            if (statusType == 0x90) {
                if (ev.data2 > 0) {
                    noteOn(ev.data1, ev.data2);
                } else {
                    noteOff(ev.data1);
                }
            } else if (statusType == 0x80) {
                noteOff(ev.data1);
            } else if (statusType == 0xB0 && ev.data1 == 123) {
                allNotesOff();
            }
        }
    }

    if (currentFrame < totalFrames) {
        renderVoices(currentFrame, totalFrames);
    }
}
