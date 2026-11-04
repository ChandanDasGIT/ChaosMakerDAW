#include "testkit.h"
#include "plugins/testsynth/testsynth.h"

#include <cmath>
#include <vector>

namespace {

struct SynthFixture {
    TestSynth synth;
    std::vector<float> left;
    std::vector<float> right;
    float* channels[2];
    MidiBuffer midi;

    SynthFixture(int frames = 480, double sampleRate = 48000.0)
        : left(frames, 0.0f), right(frames, 0.0f)
    {
        channels[0] = left.data();
        channels[1] = right.data();
        synth.prepare(sampleRate, frames);
    }

    void clearAudio() {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
    }

    ProcessContext makeContext() {
        ProcessContext ctx;
        ctx.audio = AudioView{channels, 2, static_cast<int>(left.size())};
        ctx.midi = &midi;
        ctx.sampleRate = 48000.0;
        ctx.playing = true;
        return ctx;
    }

    void process() {
        auto ctx = makeContext();
        synth.process(ctx);
    }
};

} // namespace

TEST(testsynth, sound_produced_after_note_on_and_decays_after_release)
{
    const int blockSize = 480; // 10ms at 48kHz
    SynthFixture fixture(blockSize, 48000.0);

    // Initial state: silence
    fixture.process();
    for (float s : fixture.left) {
        CHECK(s == 0.0f);
    }

    // Send Note-on at offset 0: key 69 (A4), velocity 100
    fixture.midi.clear();
    fixture.midi.add(MidiEvent{0, 0x90, 69, 100});
    fixture.clearAudio();
    fixture.process();

    // The block should now have sound
    float maxAmp = 0.0f;
    for (float s : fixture.left) {
        maxAmp = std::max(maxAmp, std::abs(s));
    }
    CHECK(maxAmp > 0.01f);

    // Continue sustaining for 5 blocks (~50ms)
    fixture.midi.clear();
    for (int i = 0; i < 5; ++i) {
        fixture.clearAudio();
        fixture.process();
    }
    // Still sounding during sustain
    maxAmp = 0.0f;
    for (float s : fixture.left) {
        maxAmp = std::max(maxAmp, std::abs(s));
    }
    CHECK(maxAmp > 0.1f);

    // Send Note-off at offset 0
    fixture.midi.clear();
    fixture.midi.add(MidiEvent{0, 0x80, 69, 0});
    fixture.clearAudio();
    fixture.process();

    // Process blocks until release completes (release is 50ms = 2400 frames = 5 blocks of 480)
    // Run 10 blocks (100ms) to guarantee complete decay to silence
    fixture.midi.clear();
    for (int i = 0; i < 10; ++i) {
        fixture.clearAudio();
        fixture.process();
    }

    // Now it should be completely silent
    float postReleaseMax = 0.0f;
    for (float s : fixture.left) {
        postReleaseMax = std::max(postReleaseMax, std::abs(s));
    }
    CHECK(postReleaseMax == 0.0f);
}

TEST(testsynth, pitch_accuracy_zero_crossings_for_a440)
{
    // 48000 frames = 1.0 second
    // At A4 (key 69 = 440 Hz), 1 second should have exactly 440 full sine cycles.
    const int oneSecond = 48000;
    SynthFixture fixture(oneSecond, 48000.0);

    // Note-on at offset 0: key 69 (440 Hz), full velocity
    fixture.midi.clear();
    fixture.midi.add(MidiEvent{0, 0x90, 69, 127});
    fixture.clearAudio();
    fixture.process();

    // Count positive-going zero crossings (samples crossing from <= 0 to > 0)
    int positiveCrossings = 0;
    for (int i = 1; i < oneSecond; ++i) {
        if (fixture.left[i - 1] <= 0.0f && fixture.left[i] > 0.0f) {
            ++positiveCrossings;
        }
    }

    // In 48000 samples at 440 Hz, there must be exactly 440 positive-going zero crossings
    CHECK(positiveCrossings == 440);
}

TEST(testsynth, reset_silences_all_voices_immediately)
{
    SynthFixture fixture(480, 48000.0);

    fixture.midi.clear();
    fixture.midi.add(MidiEvent{0, 0x90, 60, 100});
    fixture.clearAudio();
    fixture.process();

    float maxAmp = 0.0f;
    for (float s : fixture.left) maxAmp = std::max(maxAmp, std::abs(s));
    CHECK(maxAmp > 0.01f);

    // Call reset
    fixture.synth.reset();

    // Next block should be silent without any decay
    fixture.midi.clear();
    fixture.clearAudio();
    fixture.process();

    for (float s : fixture.left) {
        CHECK(s == 0.0f);
    }
}

TEST(testsynth, all_notes_off_cc_releases_voices)
{
    SynthFixture fixture(480, 48000.0);

    // Start 3 notes
    fixture.midi.clear();
    fixture.midi.add(MidiEvent{0, 0x90, 60, 100});
    fixture.midi.add(MidiEvent{0, 0x90, 64, 100});
    fixture.midi.add(MidiEvent{0, 0x90, 67, 100});
    fixture.clearAudio();
    fixture.process();

    // Verify voices are active
    int activeCount = 0;
    for (const auto& v : fixture.synth.voices()) {
        if (v.active) ++activeCount;
    }
    CHECK(activeCount == 3);

    // Send CC 123 (All Notes Off)
    fixture.midi.clear();
    fixture.midi.add(MidiEvent{0, 0xB0, 123, 0});
    fixture.clearAudio();
    fixture.process();

    // Every active voice should now be in Release
    for (const auto& v : fixture.synth.voices()) {
        if (v.active) {
            CHECK(v.state == TestSynth::Voice::State::Release);
        }
    }

    // Let decay finish
    fixture.midi.clear();
    for (int i = 0; i < 10; ++i) {
        fixture.clearAudio();
        fixture.process();
    }
    for (float s : fixture.left) {
        CHECK(s == 0.0f);
    }
}

TEST(testsynth, voice_stealing_when_exceeding_pool)
{
    SynthFixture fixture(480, 48000.0);

    // Trigger 16 distinct notes to fill pool
    fixture.midi.clear();
    for (int i = 0; i < 16; ++i) {
        fixture.midi.add(MidiEvent{0, 0x90, static_cast<uint8_t>(40 + i), 100});
    }
    fixture.clearAudio();
    fixture.process();

    int activeCount = 0;
    for (const auto& v : fixture.synth.voices()) {
        if (v.active) ++activeCount;
    }
    CHECK(activeCount == 16);

    // Trigger 17th note (key 80)
    fixture.midi.clear();
    fixture.midi.add(MidiEvent{0, 0x90, 80, 100});
    fixture.clearAudio();
    fixture.process();

    // 16 voices remain active, and one of them is key 80
    activeCount = 0;
    bool foundKey80 = false;
    for (const auto& v : fixture.synth.voices()) {
        if (v.active) {
            ++activeCount;
            if (v.key == 80) foundKey80 = true;
        }
    }
    CHECK(activeCount == 16);
    CHECK(foundKey80);
}
