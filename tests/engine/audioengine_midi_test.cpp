#include "testkit.h"
#include "engine/audioengine.h"
#include "processor/processor.h"
#include "plugins/testsynth/testsynth.h"

#include <cmath>
#include <vector>

namespace {

class MidiRecordingProcessor : public Processor {
public:
    std::string typeId() const override { return "midi_recorder"; }
    void prepare(double, int) override {}
    void reset() noexcept override {
        wasReset = true;
    }
    void process(ProcessContext& context) noexcept override {
        processCalls++;
        lastBeat = context.beat;
        if (context.midi) {
            for (const auto& ev : context.midi->events()) {
                events.push_back({context.samplePosition, ev});
            }
        }
    }

    struct Recorded {
        std::int64_t blockPosition = 0;
        MidiEvent event;
    };

    bool wasReset = false;
    int processCalls = 0;
    double lastBeat = 0.0;
    std::vector<Recorded> events;
};

void renderEngine(AudioEngine& engine, int frames)
{
    float left[1024]{}, right[1024]{};
    float* out[]{left, right};
    AudioIO io{{nullptr, 0, 0}, {out, 2, frames}, nullptr, frames};
    engine.renderBlock(io);
}

} // namespace

TEST(audioengine_midi, note_events_exact_offsets_and_multiblock_spanning)
{
    for (bool multi : {false, true}) {
        AudioEngine engine{nullptr};
        engine.deviceChanged(48000.0, 128);

        auto rec = std::make_shared<MidiRecordingProcessor>();

        auto snapshot = std::make_unique<EngineSnapshot>();
        snapshot->multiMonitor = multi;
        snapshot->playbackEnd = 1000;
        snapshot->tempo.set(120.0, 4, 4, 48000.0);

        TrackRender track;
        track.trackId = 1;
        track.chain.push_back(rec.get());

        // Note 1: on = 20, off = 80 (both within block 0 [0, 128))
        // Note 2: on = 50, off = 200 (on in block 0 [0, 128), off in block 1 [128, 256))
        // Note 3: Note off at 150, Note 4 Note on at 150 (same offset in block 1 [128, 256))
        track.notes = {
            NoteRender{20, 80, 60, 100},
            NoteRender{50, 200, 64, 110},
            NoteRender{150, 220, 67, 90}
        };
        track.noteOffs = {
            NoteRender{20, 80, 60, 100},
            NoteRender{150, 220, 67, 90},
            NoteRender{50, 200, 64, 110}
        };

        // Note A ending at 150, Note 3 starting at 150:
        // Add note A: on = 10, off = 150, key = 65
        track.notes.push_back(NoteRender{10, 150, 65, 95});
        track.noteOffs.push_back(NoteRender{10, 150, 65, 95});

        std::sort(track.notes.begin(), track.notes.end(), [](const NoteRender& a, const NoteRender& b) {
            return a.on < b.on;
        });
        std::sort(track.noteOffs.begin(), track.noteOffs.end(), [](const NoteRender& a, const NoteRender& b) {
            return a.off < b.off;
        });

        snapshot->tracks.push_back(track);
        snapshot->keepProcessors.push_back(rec);

        engine.publishPlayback(std::move(snapshot));

        // When stopped: rendering produces no midi events
        renderEngine(engine, 128);
        CHECK(rec->events.empty());
        CHECK(rec->processCalls == 0);

        // Start playback
        CHECK(engine.post({EngineCommand::Type::Play}));

        // Block 0: frames [0, 128)
        renderEngine(engine, 128);
        CHECK(engine.position() == 128);

        // In Block 0, we expect:
        // on=10 (key 65, offset 10)
        // on=20 (key 60, offset 20)
        // on=50 (key 64, offset 50)
        // off=80 (key 60, offset 80)
        CHECK(rec->events.size() == 4);
        CHECK(rec->events[0].event.offset == 10);
        CHECK(rec->events[0].event.status == 0x90);
        CHECK(rec->events[0].event.data1 == 65);
        CHECK(rec->events[0].event.data2 == 95);

        CHECK(rec->events[1].event.offset == 20);
        CHECK(rec->events[1].event.status == 0x90);
        CHECK(rec->events[1].event.data1 == 60);

        CHECK(rec->events[2].event.offset == 50);
        CHECK(rec->events[2].event.status == 0x90);
        CHECK(rec->events[2].event.data1 == 64);

        CHECK(rec->events[3].event.offset == 80);
        CHECK(rec->events[3].event.status == 0x80);
        CHECK(rec->events[3].event.data1 == 60);

        // Block 1: frames [128, 256)
        renderEngine(engine, 128);
        CHECK(engine.position() == 256);

        // In Block 1:
        // At sample 150 (offset = 150 - 128 = 22):
        // Note-off for key 65 (off=150)
        // Note-on for key 67 (on=150)
        // Note-off must come BEFORE Note-on at identical offset!
        // Also off=200 (key 64, offset = 200 - 128 = 72)
        // Also off=220 (key 67, offset = 220 - 128 = 92)
        // Total events so far: 4 from block 0 + 4 from block 1 = 8
        CHECK(rec->events.size() == 8);

        // Event 4: Note-off for key 65 at offset 22
        CHECK(rec->events[4].blockPosition == 128);
        CHECK(rec->events[4].event.offset == 22);
        CHECK(rec->events[4].event.status == 0x80);
        CHECK(rec->events[4].event.data1 == 65);

        // Event 5: Note-on for key 67 at offset 22
        CHECK(rec->events[5].blockPosition == 128);
        CHECK(rec->events[5].event.offset == 22);
        CHECK(rec->events[5].event.status == 0x90);
        CHECK(rec->events[5].event.data1 == 67);

        // Event 6: Note-off for key 64 at offset 72
        CHECK(rec->events[6].blockPosition == 128);
        CHECK(rec->events[6].event.offset == 72);
        CHECK(rec->events[6].event.status == 0x80);
        CHECK(rec->events[6].event.data1 == 64);

        // Event 7: Note-off for key 67 at offset 92
        CHECK(rec->events[7].blockPosition == 128);
        CHECK(rec->events[7].event.offset == 92);
        CHECK(rec->events[7].event.status == 0x80);
        CHECK(rec->events[7].event.data1 == 67);

        engine.stop();
    }
}

TEST(audioengine_midi, loop_wrap_and_reset_produce_all_notes_off_and_reset)
{
    for (bool multi : {false, true}) {
        AudioEngine engine{nullptr};
        engine.deviceChanged(48000.0, 128);

        auto rec = std::make_shared<MidiRecordingProcessor>();

        auto snapshot = std::make_unique<EngineSnapshot>();
        snapshot->multiMonitor = multi;
        snapshot->playbackEnd = 1000;
        snapshot->tempo.set(120.0, 4, 4, 48000.0);

        TrackRender track;
        track.trackId = 1;
        track.chain.push_back(rec.get());

        // Note starting at 100 and ending at 400
        track.notes = { NoteRender{100, 400, 60, 100} };
        track.noteOffs = { NoteRender{100, 400, 60, 100} };

        snapshot->tracks.push_back(track);
        snapshot->keepProcessors.push_back(rec);

        engine.publishPlayback(std::move(snapshot));

        // Set loop from 0 to 256
        CHECK(engine.post({EngineCommand::Type::SetLoop, 0, 256}));
        CHECK(engine.post({EngineCommand::Type::Play}));

        // Block 0 [0, 128)
        renderEngine(engine, 128);
        CHECK(engine.position() == 128);

        // Block 1 [128, 256)
        renderEngine(engine, 128);
        CHECK(engine.position() == 256);

        // Block 2: Loop wraps to 0. Position becomes 128.
        // It must output an all-notes-off CC (0xB0, 123, 0) at offset 0!
        renderEngine(engine, 128);
        CHECK(engine.position() == 128);

        bool foundAllNotesOff = false;
        for (const auto& r : rec->events) {
            if (r.blockPosition == 0 && (r.event.status & 0xF0) == 0xB0 && r.event.data1 == 123) {
                foundAllNotesOff = true;
                CHECK(r.event.offset == 0);
            }
        }
        CHECK(foundAllNotesOff);

        // Stop triggers reset()
        rec->wasReset = false;
        engine.stop();
        // Seek triggers reset()
        CHECK(engine.post({EngineCommand::Type::Seek, 50}));
        renderEngine(engine, 128);
        CHECK(rec->wasReset);
    }
}

TEST(audioengine_midi, pattern_rendering_produces_sound_with_exact_timing)
{
    for (bool multi : {false, true}) {
        AudioEngine engine{nullptr};
        engine.deviceChanged(48000.0, 128);

        auto synth = std::make_shared<TestSynth>();
        synth->prepare(48000.0, 128);

        auto snapshot = std::make_unique<EngineSnapshot>();
        snapshot->multiMonitor = multi;
        snapshot->playbackEnd = 1000;
        snapshot->tempo.set(120.0, 4, 4, 48000.0);

        TrackRender track;
        track.trackId = 1;
        track.chain.push_back(synth.get());

        // Note starts at sample 50 and ends at 200
        track.notes = { NoteRender{50, 200, 69, 100} };
        track.noteOffs = { NoteRender{50, 200, 69, 100} };

        snapshot->tracks.push_back(track);
        snapshot->keepProcessors.push_back(synth);

        engine.publishPlayback(std::move(snapshot));
        CHECK(engine.post({EngineCommand::Type::Play}));

        float left[128]{}, right[128]{};
        float* out[]{left, right};
        AudioIO io{{nullptr, 0, 0}, {out, 2, 128}, nullptr, 128};
        engine.renderBlock(io);

        // Prior to sample 50: silence
        for (int i = 0; i < 50; ++i) {
            CHECK(left[i] == 0.0f);
            CHECK(right[i] == 0.0f);
        }

        // Voice started at sample 50 with phase 0 (sin(0) = 0).
        // By sample 52, it must have non-zero amplitude!
        bool soundDetected = false;
        for (int i = 51; i < 128; ++i) {
            if (std::abs(left[i]) > 1e-4f && std::abs(right[i]) > 1e-4f) {
                soundDetected = true;
                break;
            }
        }
        CHECK(soundDetected);
        engine.stop();
    }
}

TEST(audioengine_midi, mute_and_solo_silence_instrument_tracks)
{
    for (bool multi : {false, true}) {
        // Case 1: Track is muted -> produces 0.0f output
        {
            AudioEngine engine{nullptr};
            engine.deviceChanged(48000.0, 128);

            auto synth = std::make_shared<TestSynth>();
            synth->prepare(48000.0, 128);

            auto snapshot = std::make_unique<EngineSnapshot>();
            snapshot->multiMonitor = multi;
            snapshot->playbackEnd = 1000;
            snapshot->tempo.set(120.0, 4, 4, 48000.0);

            TrackRender track;
            track.trackId = 1;
            track.muted = true; // MUTED
            track.chain.push_back(synth.get());
            track.notes = { NoteRender{10, 100, 69, 100} };
            track.noteOffs = { NoteRender{10, 100, 69, 100} };

            snapshot->tracks.push_back(track);
            snapshot->keepProcessors.push_back(synth);

            engine.publishPlayback(std::move(snapshot));
            CHECK(engine.post({EngineCommand::Type::Play}));

            float left[128]{}, right[128]{};
            float* out[]{left, right};
            AudioIO io{{nullptr, 0, 0}, {out, 2, 128}, nullptr, 128};
            engine.renderBlock(io);

            // Output must be completely silent because track is muted
            for (int i = 0; i < 128; ++i) {
                CHECK(left[i] == 0.0f);
                CHECK(right[i] == 0.0f);
            }
            engine.stop();
        }

        // Case 2: Another track is soloed, this track is not -> produces 0.0f output
        {
            AudioEngine engine{nullptr};
            engine.deviceChanged(48000.0, 128);

            auto synth = std::make_shared<TestSynth>();
            synth->prepare(48000.0, 128);

            auto snapshot = std::make_unique<EngineSnapshot>();
            snapshot->multiMonitor = multi;
            snapshot->playbackEnd = 1000;
            snapshot->tempo.set(120.0, 4, 4, 48000.0);

            // Track 1: Synth (not soloed)
            TrackRender track1;
            track1.trackId = 1;
            track1.soloed = false;
            track1.chain.push_back(synth.get());
            track1.notes = { NoteRender{10, 100, 69, 100} };
            track1.noteOffs = { NoteRender{10, 100, 69, 100} };

            // Track 2: Other track (soloed)
            TrackRender track2;
            track2.trackId = 2;
            track2.soloed = true;

            snapshot->tracks.push_back(track1);
            snapshot->tracks.push_back(track2);
            snapshot->keepProcessors.push_back(synth);

            engine.publishPlayback(std::move(snapshot));
            CHECK(engine.post({EngineCommand::Type::Play}));

            float left[128]{}, right[128]{};
            float* out[]{left, right};
            AudioIO io{{nullptr, 0, 0}, {out, 2, 128}, nullptr, 128};
            engine.renderBlock(io);

            // Output must be completely silent because another track is soloed
            for (int i = 0; i < 128; ++i) {
                CHECK(left[i] == 0.0f);
                CHECK(right[i] == 0.0f);
            }
            engine.stop();
        }
    }
}

TEST(audioengine_midi, live_note_audition_when_stopped)
{
    for (bool multi : {false, true}) {
        AudioEngine engine{nullptr};
        engine.deviceChanged(48000.0, 128);

        auto synth = std::make_shared<TestSynth>();
        synth->prepare(48000.0, 128);

        auto snapshot = std::make_unique<EngineSnapshot>();
        snapshot->multiMonitor = multi;
        snapshot->tempo.set(120.0, 4, 4, 48000.0);

        TrackRender track;
        track.trackId = 42;
        track.chain.push_back(synth.get());
        snapshot->tracks.push_back(track);
        snapshot->keepProcessors.push_back(synth);

        engine.publishPlayback(std::move(snapshot));

        float left[128]{}, right[128]{};
        float* out[]{left, right};
        AudioIO io{{nullptr, 0, 0}, {out, 2, 128}, nullptr, 128};

        // Initially stopped: buffer should be silence
        engine.renderBlock(io);
        for (int i = 0; i < 128; ++i) {
            CHECK(left[i] == 0.0f);
            CHECK(right[i] == 0.0f);
        }

        // Post NoteFromUi Note-On: trackId=42, status=0x90, key=60, vel=100
        EngineCommand cmdOn;
        cmdOn.type = EngineCommand::Type::NoteFromUi;
        cmdOn.a = 42;
        cmdOn.b = (0x90 << 16) | (60 << 8) | 100;
        CHECK(engine.post(cmdOn));

        // Render block while stopped
        std::fill_n(left, 128, 0.0f);
        std::fill_n(right, 128, 0.0f);
        engine.renderBlock(io);

        // Audio should now be non-zero
        float maxAmp = 0.0f;
        for (int i = 0; i < 128; ++i) {
            maxAmp = std::max(maxAmp, std::abs(left[i]));
        }
        CHECK(maxAmp > 0.01f);

        // Sustaining block (no new events posted, key still held)
        std::fill_n(left, 128, 0.0f);
        std::fill_n(right, 128, 0.0f);
        engine.renderBlock(io);
        maxAmp = 0.0f;
        for (int i = 0; i < 128; ++i) {
            maxAmp = std::max(maxAmp, std::abs(left[i]));
        }
        CHECK(maxAmp > 0.01f);

        // Post NoteFromUi Note-Off
        EngineCommand cmdOff;
        cmdOff.type = EngineCommand::Type::NoteFromUi;
        cmdOff.a = 42;
        cmdOff.b = (0x80 << 16) | (60 << 8) | 0;
        CHECK(engine.post(cmdOff));

        // Render block with Note-Off: release decay starts, still audible
        std::fill_n(left, 128, 0.0f);
        std::fill_n(right, 128, 0.0f);
        engine.renderBlock(io);
        maxAmp = 0.0f;
        for (int i = 0; i < 128; ++i) {
            maxAmp = std::max(maxAmp, std::abs(left[i]));
        }
        CHECK(maxAmp > 0.0f);
    }
}

TEST(audioengine_midi, hardware_midi_input_routing)
{
    for (bool multi : {false, true}) {
        AudioEngine engine{nullptr};
        engine.deviceChanged(48000.0, 128);

        auto synth = std::make_shared<TestSynth>();
        synth->prepare(48000.0, 128);

        auto snapshot = std::make_unique<EngineSnapshot>();
        snapshot->multiMonitor = multi;
        snapshot->tempo.set(120.0, 4, 4, 48000.0);

        TrackRender track;
        track.trackId = 10;
        track.armed = true;
        track.chain.push_back(synth.get());
        snapshot->tracks.push_back(track);
        snapshot->keepProcessors.push_back(synth);

        engine.publishPlayback(std::move(snapshot));

        float left[128]{}, right[128]{};
        float* out[]{left, right};

        // Hardware MIDI buffer with a Note-On at offset 16 (Middle C, vel 90)
        MidiBuffer hwMidi;
        hwMidi.add(MidiEvent{16, 0x90, 60, 90});

        AudioIO io{{nullptr, 0, 0}, {out, 2, 128}, &hwMidi, 128};

        // Render block with hardware MIDI input while stopped
        engine.renderBlock(io);

        float maxAmp = 0.0f;
        for (int i = 0; i < 128; ++i) {
            maxAmp = std::max(maxAmp, std::abs(left[i]));
        }
        CHECK(maxAmp > 0.01f);

        // Next block: empty hardware MIDI buffer (note sustaining)
        hwMidi.clear();
        std::fill_n(left, 128, 0.0f);
        std::fill_n(right, 128, 0.0f);
        engine.renderBlock(io);

        maxAmp = 0.0f;
        for (int i = 0; i < 128; ++i) {
            maxAmp = std::max(maxAmp, std::abs(left[i]));
        }
        CHECK(maxAmp > 0.01f);

        // Note-off via hardware MIDI
        hwMidi.add(MidiEvent{32, 0x80, 60, 0});
        std::fill_n(left, 128, 0.0f);
        std::fill_n(right, 128, 0.0f);
        engine.renderBlock(io);

        maxAmp = 0.0f;
        for (int i = 0; i < 128; ++i) {
            maxAmp = std::max(maxAmp, std::abs(left[i]));
        }
        CHECK(maxAmp > 0.0f);
    }
}

TEST(audioengine_midi, typing_keyboard_notes_join_the_midi_input)
{
    AudioEngine engine{nullptr};
    engine.deviceChanged(48000.0, 128);
    auto armedSynth = std::make_shared<TestSynth>(), otherSynth = std::make_shared<TestSynth>();
    armedSynth->prepare(48000.0, 128);
    otherSynth->prepare(48000.0, 128);
    auto snapshot = std::make_unique<EngineSnapshot>();
    snapshot->tempo.set(120.0, 4, 4, 48000.0);
    TrackRender armed, other;
    armed.trackId = 1; armed.armed = true; armed.chain.push_back(armedSynth.get());
    other.trackId = 2; other.chain.push_back(otherSynth.get());     // neither armed nor monitored
    snapshot->tracks.push_back(armed);
    snapshot->tracks.push_back(other);
    snapshot->keepProcessors.push_back(armedSynth);
    snapshot->keepProcessors.push_back(otherSynth);
    engine.publishPlayback(std::move(snapshot));

    float left[128]{}, right[128]{};
    float* out[]{left, right};
    MidiBuffer device;                                              // a MIDI keyboard, same block
    device.add(MidiEvent{16, 0x90, 64, 90});
    AudioIO io{{nullptr, 0, 0}, {out, 2, 128}, &device, 128};
    const auto sounding = [](const TestSynth& synth, int key) {
        for (const auto& v : synth.voices()) if (v.active && v.key == key && v.state != TestSynth::Voice::State::Release) return true;
        return false;
    };

    CHECK(engine.sendKeyboardMidi(MidiEvent{0, 0x90, 60, 100}));
    engine.renderBlock(io);
    CHECK(sounding(*armedSynth, 60));                               // typed note
    CHECK(sounding(*armedSynth, 64));                               // the device's note too
    CHECK(!sounding(*otherSynth, 60));                              // routed like hardware MIDI

    device.clear();
    CHECK(engine.sendKeyboardMidi(MidiEvent{0, 0x80, 60, 0}));
    engine.renderBlock(io);
    CHECK(!sounding(*armedSynth, 60));
    CHECK(sounding(*armedSynth, 64));

    io.midiIn = nullptr;                                            // no MIDI device at all
    CHECK(engine.sendKeyboardMidi(MidiEvent{0, 0x90, 67, 100}));
    engine.renderBlock(io);
    CHECK(sounding(*armedSynth, 67));
}

TEST(audioengine_midi, playback_stops_at_the_end_and_instruments_ring_out)
{
    for (bool multi : {false, true}) {
        AudioEngine engine{nullptr};
        engine.deviceChanged(48000.0, 128);
        auto rec = std::make_shared<MidiRecordingProcessor>();
        auto snapshot = std::make_unique<EngineSnapshot>();
        snapshot->multiMonitor = multi;
        snapshot->playbackEnd = 1000;                           // the last clip's end
        snapshot->tempo.set(120.0, 4, 4, 48000.0);
        TrackRender track;
        track.trackId = 1;
        track.chain.push_back(rec.get());
        track.notes = {NoteRender{100, 200, 60, 100}, NoteRender{900, 1000, 64, 100}};   // the second lasts to the end
        track.noteOffs = track.notes;
        snapshot->tracks.push_back(track);
        snapshot->keepProcessors.push_back(rec);
        engine.publishPlayback(std::move(snapshot));

        CHECK(engine.post({EngineCommand::Type::Play}));
        for (int i = 0; i < 9; ++i) { renderEngine(engine, 128); if (!engine.playing()) break; }   // Play starts in the first block
        CHECK(!engine.playing());
        CHECK(engine.position() == 1000);                       // stopped exactly at the end
        // The ring-out starts in the same callback: the note still held gets its note-off at
        // the end, at once.
        bool released = false;
        for (const auto& e : rec->events)
            released |= e.blockPosition == 1000 && e.event.status == 0x80 && e.event.data1 == 64 && e.event.offset == 0;
        CHECK(released);
        rec->wasReset = false;
        rec->events.clear();

        int calls = rec->processCalls;
        renderEngine(engine, 128);                              // still processed while stopped
        CHECK(engine.position() == 1000);
        CHECK(rec->processCalls > calls);
        CHECK(!rec->wasReset);                                  // the ring is not cut
        CHECK(rec->events.empty());                             // the note-off came once

        for (int i = 0; i < 48000 * 2 / 128 + 2; ++i) renderEngine(engine, 128);
        calls = rec->processCalls;
        renderEngine(engine, 128);
        CHECK(rec->processCalls == calls);                      // two seconds later: done
        CHECK(engine.position() == 1000);

        // Stop during a ring-out ends it at once.
        CHECK(engine.post({EngineCommand::Type::Seek, 0}));
        CHECK(engine.post({EngineCommand::Type::Play}));
        for (int i = 0; i < 9; ++i) { renderEngine(engine, 128); if (!engine.playing()) break; }
        CHECK(!engine.playing());
        CHECK(engine.post({EngineCommand::Type::Stop}));
        renderEngine(engine, 128);
        calls = rec->processCalls;
        renderEngine(engine, 128);
        CHECK(rec->processCalls == calls);
    }
}

TEST(audioengine_midi, a_processor_that_keeps_running_is_processed_while_stopped)
{
    // The Chords track's latched autoplay plays with no key held and the transport stopped:
    // its processor says so (keepsRunning) and the engine keeps calling it, in both mixing paths.
    struct Runner : MidiRecordingProcessor {
        bool running = false;
        bool keepsRunning() const noexcept override { return running; }
    };
    for (bool multi : {false, true}) {
        AudioEngine engine{nullptr};
        engine.deviceChanged(48000.0, 128);
        auto runner = std::make_shared<Runner>();
        auto snapshot = std::make_unique<EngineSnapshot>();
        snapshot->multiMonitor = multi;
        snapshot->tempo.set(120.0, 4, 4, 48000.0);
        TrackRender track;
        track.trackId = 1;
        track.chain.push_back(runner.get());
        snapshot->tracks.push_back(track);
        snapshot->keepProcessors.push_back(runner);
        engine.publishPlayback(std::move(snapshot));

        for (int i = 0; i < 4; ++i) renderEngine(engine, 128);
        CHECK(runner->processCalls == 0);                           // stopped, nothing held: idle
        runner->running = true;
        for (int i = 0; i < 4; ++i) renderEngine(engine, 128);
        CHECK(runner->processCalls >= 4);
        const int calls = runner->processCalls;
        runner->running = false;
        for (int i = 0; i < 4; ++i) renderEngine(engine, 128);
        CHECK(runner->processCalls == calls);                       // idle again
    }
}

namespace {
std::shared_ptr<const AudioData> constantAudio(int frames)
{
    auto data = std::make_shared<AudioData>();
    data->sampleRate = 48000;
    data->frames = frames;
    data->channels = {std::vector<float>(frames, 0.5f), std::vector<float>(frames, 0.5f)};
    return data;
}

// One audio track playing a constant 0.5 clip under the given envelope; returns 512 rendered
// left-channel samples (the track at the fader's 1.0, centre pan).
std::vector<float> renderWithEnvelope(bool multi, const automation::VolumeEnvelope& envelope, bool play = true)
{
    AudioEngine engine{nullptr};
    engine.deviceChanged(48000.0, 512);
    auto audio = constantAudio(48000);
    auto snapshot = std::make_unique<EngineSnapshot>();
    snapshot->multiMonitor = multi;
    snapshot->tempo.set(120.0, 4, 4, 48000.0);
    TrackRender track;
    track.trackId = 1;
    track.clips.push_back({audio.get(), 0, 48000, 0, 0, 0, 1.0f, true});
    track.envelope = envelope;
    snapshot->tracks.push_back(std::move(track));
    snapshot->keepAudio.push_back(audio);
    engine.publishPlayback(std::move(snapshot));
    if (play) CHECK(engine.post({EngineCommand::Type::Play}));
    float left[512]{}, right[512]{};
    float* out[]{left, right};
    AudioIO io{{nullptr, 0, 0}, {out, 2, 512}, nullptr, 512};
    engine.renderBlock(io);
    engine.stop();
    return {left, left + 512};
}
} // namespace

TEST(audioengine_midi, volume_envelope_fades_the_track_and_bypass_leaves_it_alone)
{
    using automation::envelopescale::unityLevel;
    for (bool multi : {false, true}) {
        automation::VolumeEnvelope none;
        const auto plain = renderWithEnvelope(multi, none);
        CHECK_NEAR(plain[0], 0.5f, 1e-5);
        CHECK_NEAR(plain[511], 0.5f, 1e-5);

        // A fade from 0 dB at sample 0 to silence at sample 512.
        automation::VolumeEnvelope fade;
        CHECK(fade.addPoint(0, unityLevel()) == 0);
        CHECK(fade.addPoint(512, 0.0) == 1);
        const auto faded = renderWithEnvelope(multi, fade);
        CHECK_NEAR(faded[0], 0.5f, 1e-5);
        for (int i = 1; i < 512; ++i) {
            CHECK(faded[i] <= faded[i - 1] + 1e-7f);   // only goes down
            CHECK_NEAR(faded[i], float(0.5 * fade.gainAt(i)), 1e-5);   // the drawn line's scale
        }
        CHECK(faded[500] < 0.001f);

        fade.setBypassed(true);
        const auto bypassed = renderWithEnvelope(multi, fade);
        for (int i = 0; i < 512; ++i) CHECK_NEAR(bypassed[i], plain[i], 1e-6);

        // A boost: +6 dB everywhere doubles the level (0.5 -> about 1.0).
        automation::VolumeEnvelope boost;
        boost.addPoint(0, 1.0);
        const auto boosted = renderWithEnvelope(multi, boost);
        CHECK_NEAR(boosted[100], float(0.5 * std::pow(10.0, 6.0 / 20.0)), 1e-4);
    }
}
