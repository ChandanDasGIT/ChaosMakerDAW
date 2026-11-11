#include "testkit.h"
#include "audiofile/audioimport.h"
#include "audiofile/wavreader.h"
#include "audiofile/wavwriter.h"
#include "recording/takerecorder.h"
#include "session/tracklist.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace {

struct TempDir {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("chaosmaker-import-" + std::to_string(getpid()));
    TempDir() { std::filesystem::remove_all(path); std::filesystem::create_directories(path); }
    ~TempDir() { std::filesystem::remove_all(path); }
    std::string file(const std::string& name) const { return (path / name).string(); }
};

std::vector<std::vector<float>> sine(int channels, int frames, double rate)
{
    std::vector<std::vector<float>> out(static_cast<std::size_t>(channels), std::vector<float>(static_cast<std::size_t>(frames)));
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i) out[std::size_t(c)][std::size_t(i)] = float(0.5 * std::sin(2 * M_PI * 440.0 * (c + 1) * i / rate));
    return out;
}

void writeBytes(const std::string& path, const std::vector<unsigned char>& bytes)
{
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

} // namespace

TEST(audioimport, written_wavs_read_back_at_every_bit_depth)
{
    TempDir dir;
    const auto audio = sine(2, 4410, 44100.0);
    struct Case { audiofile::WavFormat format; float tolerance; int bits; };
    for (const auto& c : {Case{audiofile::WavFormat::Float32, 1e-7f, 32}, Case{audiofile::WavFormat::Int24, 2e-7f, 24},
                          Case{audiofile::WavFormat::Int16, 1.0f / 16384.0f, 16}}) {
        const auto path = dir.file("t" + std::to_string(c.bits) + ".wav");
        audiofile::writeWav(path, audio, 44100.0, c.format);
        const auto read = audioimport::read(path);
        CHECK(read->channels.size() == 2 && read->frames == 4410 && read->sampleRate == 44100.0);
        float worst = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 4410; ++i) worst = std::max(worst, std::abs(read->channels[std::size_t(ch)][std::size_t(i)] - audio[std::size_t(ch)][std::size_t(i)]));
        CHECK(worst <= c.tolerance);
        CHECK(!read->peaks.empty());                                 // the waveform is built
        CHECK(audiofile::readWav(path).bitsPerSample == c.bits);
    }
    // The float layout is the one a reopened project reads (the recordings' format).
    audiofile::writeWav(dir.file("take.wav"), audio, 48000.0, audiofile::WavFormat::Float32);
    const auto take = loadTakeWav(dir.file("take.wav"));
    CHECK(take->frames == 4410 && take->channels.size() == 2 && take->channels[1][100] == audio[1][100]);
}

TEST(audioimport, files_are_recognised_by_content_and_bad_ones_say_why)
{
    TempDir dir;
    const std::vector<unsigned char> wav{'R','I','F','F',0,0,0,0,'W','A','V','E'};
    const std::vector<unsigned char> flac{'f','L','a','C',0};
    const std::vector<unsigned char> mp3{0xFF, 0xFB, 0x90, 0x00};
    const std::vector<unsigned char> id3mp3{'I','D','3',4,0,0, 0,0,0,2, 0,0, 0xFF,0xFB,0x90,0};
    CHECK(audioimport::detect(wav) == audioimport::Format::Wav);
    CHECK(audioimport::detect(flac) == audioimport::Format::Flac);
    CHECK(audioimport::detect(mp3) == audioimport::Format::Mp3);
    CHECK(audioimport::detect(id3mp3) == audioimport::Format::Mp3);
    CHECK(audioimport::detect(std::vector<unsigned char>{'h','e','l','l','o'}) == audioimport::Format::Unknown);

    // A WAV named .mp3 is still read as a WAV; text named .wav is refused by name.
    audiofile::writeWav(dir.file("really-a-wav.mp3"), sine(1, 1000, 48000), 48000, audiofile::WavFormat::Float32);
    CHECK(audioimport::read(dir.file("really-a-wav.mp3"))->frames == 1000);
    writeBytes(dir.file("notes.wav"), {'h','e','l','l','o',' ','w','o','r','l','d'});
    std::string message;
    try { audioimport::read(dir.file("notes.wav")); } catch (const std::exception& e) { message = e.what(); }
    CHECK(message.find("notes.wav") != std::string::npos);
    message.clear();
    try { audioimport::read(dir.file("missing.wav")); } catch (const std::exception& e) { message = e.what(); }
    CHECK(message.find("missing.wav") != std::string::npos);
    // A WAV cut short still gives the whole frames that are there.
    audiofile::writeWav(dir.file("cut.wav"), sine(1, 2000, 48000), 48000, audiofile::WavFormat::Int16);
    std::filesystem::resize_file(dir.file("cut.wav"), 44 + 1000 * 2 + 1);
    CHECK(audioimport::read(dir.file("cut.wav"))->frames == 1000);
}

TEST(audioimport, more_than_two_channels_are_mixed_down_and_takes_are_kept_uniquely)
{
    TempDir dir;
    // A 4-channel 16-bit WAV: channels 0, 1, 2, 3 at 0.1, 0.2, 0.3, 0.4.
    std::vector<unsigned char> b{'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0, 1,0, 4,0,
                                 0x80,0xBB,0,0, 0x00,0xEE,0x02,0, 8,0, 16,0, 'd','a','t','a', 0x40,0x1F,0,0};
    for (int f = 0; f < 1000; ++f)
        for (int c = 0; c < 4; ++c) {
            const int v = int(std::lround(0.1 * (c + 1) * 32767));
            b.push_back((unsigned char)(v & 0xff)); b.push_back((unsigned char)((v >> 8) & 0xff));
        }
    writeBytes(dir.file("quad.wav"), b);
    const auto audio = audioimport::read(dir.file("quad.wav"));
    CHECK(audio->channels.size() == 2);
    CHECK(std::abs(audio->channels[0][10] - 0.2f) < 1e-3f);     // (0.1 + 0.3) / 2
    CHECK(std::abs(audio->channels[1][10] - 0.3f) < 1e-3f);     // (0.2 + 0.4) / 2

    const auto first = audioimport::saveAsTake(*audio, dir.file("media"), "quad.wav");
    const auto second = audioimport::saveAsTake(*audio, dir.file("media"), "quad.wav");
    CHECK(first->path != second->path);
    CHECK(std::filesystem::exists(first->path) && std::filesystem::exists(second->path));
    CHECK(loadTakeWav(first->path)->frames == 1000);
}

TEST(audioimport, an_import_adds_tracks_or_clips_in_one_undo_step)
{
    TrackList tracks;
    tracks.setSampleRate(48000);
    const int existing = tracks.addVocalTrack();
    const auto make = [](const std::string& path, std::int64_t frames, double rate) {
        auto a = std::make_shared<AudioData>();
        a->path = path; a->frames = frames; a->sampleRate = rate;
        a->channels = {std::vector<float>(std::size_t(frames), 0.1f)};
        a->buildWaveform();
        return std::shared_ptr<const AudioData>(a);
    };
    std::vector<int> added;
    const auto clips = tracks.importAudio({make("/m/import-drums.wav", 48000, 48000), make("/m/import-bass.wav", 44100, 44100)}, 4.0, -1, &added);
    CHECK(clips.size() == 2 && added.size() == 2);
    CHECK(tracks.project().tracks().size() == 3);
    CHECK(tracks.project().findTrack(added[0])->name == "drums" && tracks.project().findTrack(added[1])->name == "bass");
    const auto* clip = tracks.findClip(added[1], clips[1]);
    CHECK(clip && clip->start == 96000 && clip->length == 48000);   // beat 4 at 120 BPM; 1 s at either rate
    CHECK(tracks.audioFor("/m/import-bass.wav") != nullptr);
    CHECK(tracks.undo());                                          // both tracks go at once
    CHECK(tracks.project().tracks().size() == 1 && tracks.project().tracks()[0]->id == existing);
    CHECK(tracks.redo());
    CHECK(tracks.project().tracks().size() == 3 && tracks.findClip(added[0], clips[0]));
    CHECK(tracks.undo());

    // Onto one track: both clips there, one undo step.
    const auto onTrack = tracks.importAudio({make("/m/a.wav", 4800, 48000), make("/m/b.wav", 4800, 48000)}, 0.0, existing);
    CHECK(onTrack.size() == 2 && tracks.project().findTrack(existing)->audioClips.size() == 2);
    CHECK(tracks.undo());
    CHECK(tracks.project().findTrack(existing)->audioClips.empty());
    CHECK(tracks.importAudio({}, 0.0).empty());
    CHECK(tracks.importAudio({make("/m/c.wav", 4800, 48000)}, -1.0).empty());
}
