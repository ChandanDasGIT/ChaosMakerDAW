#include "testkit.h"
#include "audiofile/audioimport.h"
#include "audiofile/mp3reader.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

// The MP3 decoder (restMap E: MP3). Fixtures in tests/audiofile/data, made with LAME from
// ffmpeg sines of exactly 0.5 s at amplitude 1/16 (its default 1/8, -6 dB; split to stereo at
// -3 dB per side). (The decoder was also checked against ffmpeg on
// these and on real songs: the same samples within about 120 dB.)
namespace {
const std::filesystem::path Data = std::filesystem::path(__FILE__).parent_path() / "data";

// The strongest frequency near `hz` and the level there (correlation), over the middle.
double levelAt(const std::vector<float>& x, double rate, double hz)
{
    double s = 0, c = 0;
    const std::size_t from = x.size() / 4, to = 3 * x.size() / 4;
    for (std::size_t i = from; i < to; ++i) {
        s += x[i] * std::sin(2 * M_PI * hz * double(i) / rate);
        c += x[i] * std::cos(2 * M_PI * hz * double(i) / rate);
    }
    return 2.0 * std::sqrt(s * s + c * c) / double(to - from);
}

std::vector<std::uint8_t> bytesOf(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
}

TEST(mp3reader, a_cbr_stereo_file_with_an_id3_tag_decodes_to_its_exact_length_and_tone)
{
    const auto mp3 = audiofile::readMp3((Data / "sine440-stereo-128k-id3.mp3").string());
    const auto& a = *mp3.audio;
    CHECK(mp3.version == 1 && mp3.bitrateKbps == 128 && !mp3.variableBitrate);
    CHECK(a.sampleRate == 44100.0 && a.channels.size() == 2);
    CHECK(a.frames == 22050);                                   // gapless: exactly 0.5 s
    CHECK(mp3.encoderDelay >= 0 && mp3.encoderPadding >= 0);
    for (const auto& channel : a.channels) {
        CHECK(std::abs(levelAt(channel, 44100, 440) - 0.0625 / std::sqrt(2.0)) < 0.004);   // within LAME's own 0.4 dB
        CHECK(levelAt(channel, 44100, 880) < 0.005);                  // a clean tone
    }
    // The first samples start the sine (no encoder delay left in front).
    CHECK(std::abs(a.channels[0][0]) < 0.05);
}

TEST(mp3reader, mpeg2_mono_and_vbr_files_decode)
{
    const auto lsf = audiofile::readMp3((Data / "sine1k-mono-22k-mpeg2.mp3").string());
    CHECK(lsf.version == 2 && lsf.audio->sampleRate == 22050.0 && lsf.audio->channels.size() == 1);
    CHECK(lsf.encoderDelay == -1);                               // no LAME tag here: nothing trimmed,
    CHECK(lsf.audio->frames == 22 * 576);                        // whole frames (as ffmpeg gives too)
    CHECK(std::abs(levelAt(lsf.audio->channels[0], 22050, 1000) - 0.0625) < 0.003);

    const auto vbr = audiofile::readMp3((Data / "sine440-stereo-vbr.mp3").string());
    CHECK(vbr.audio->frames == 22050);
    CHECK(std::abs(levelAt(vbr.audio->channels[1], 44100, 440) - 0.0625 / std::sqrt(2.0)) < 0.003);
}

TEST(mp3reader, the_import_reads_mp3_and_damaged_files_say_why)
{
    const auto imported = audioimport::read((Data / "sine440-stereo-128k-id3.mp3").string());
    CHECK(imported->frames == 22050 && !imported->peaks.empty());

    // Cut in the middle of a frame: the whole frames before it still decode.
    auto bytes = bytesOf(Data / "sine440-stereo-128k-id3.mp3");
    std::vector<std::uint8_t> cut(bytes.begin(), bytes.begin() + std::ptrdiff_t(bytes.size() * 2 / 3));
    const auto partial = audiofile::decodeMp3(cut, "cut.mp3");
    CHECK(partial.audio->frames > 5000 && partial.audio->frames < 22050);
    // Garbage in the middle: the decoder finds the next good frame.
    auto damaged = bytes;
    for (std::size_t i = damaged.size() / 2; i < damaged.size() / 2 + 200; ++i) damaged[i] = std::uint8_t(i * 37);
    CHECK(audiofile::decodeMp3(damaged, "damaged.mp3").audio->frames > 10000);
    // Not MP3 at all.
    std::string message;
    try { audiofile::decodeMp3(std::vector<std::uint8_t>(5000, 0x41), "words.mp3"); }
    catch (const std::exception& e) { message = e.what(); }
    CHECK(message.find("words.mp3") != std::string::npos);
}
