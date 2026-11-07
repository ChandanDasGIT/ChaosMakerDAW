#include "testkit.h"
#include "audiofile/wavreader.h"

#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using audiofile::decodeWav;
using audiofile::readWav;

namespace {

using Bytes = std::vector<std::uint8_t>;

void put16(Bytes& b, std::uint32_t v) { b.push_back(std::uint8_t(v)); b.push_back(std::uint8_t(v >> 8)); }
void put32(Bytes& b, std::uint32_t v) { put16(b, v & 0xFFFF); put16(b, v >> 16); }
void putTag(Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); }

void chunk(Bytes& b, const char* tag, const Bytes& body)
{
    putTag(b, tag);
    put32(b, std::uint32_t(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    if (body.size() & 1u) b.push_back(0);
}

Bytes fmtBody(unsigned format, unsigned channels, unsigned rate, unsigned bits)
{
    Bytes f;
    put16(f, format); put16(f, channels); put32(f, rate);
    put32(f, rate * channels * (bits / 8)); put16(f, channels * (bits / 8)); put16(f, bits);
    return f;
}

// A WAV with the given chunks after the header; the RIFF size is set from them.
Bytes riff(const std::vector<std::pair<const char*, Bytes>>& chunks)
{
    Bytes b;
    putTag(b, "RIFF"); put32(b, 0); putTag(b, "WAVE");
    for (const auto& [tag, body] : chunks) chunk(b, tag, body);
    const auto size = std::uint32_t(b.size() - 8);
    b[4] = std::uint8_t(size); b[5] = std::uint8_t(size >> 8); b[6] = std::uint8_t(size >> 16); b[7] = std::uint8_t(size >> 24);
    return b;
}

Bytes pcm16(std::initializer_list<int> samples)
{
    Bytes d;
    for (int s : samples) put16(d, std::uint32_t(std::uint16_t(std::int16_t(s))));
    return d;
}

bool throws(const Bytes& bytes)
{
    try { decodeWav(bytes, "bad.wav"); } catch (const std::runtime_error& e) {
        return std::string(e.what()).rfind("bad.wav: ", 0) == 0;   // names the file
    }
    return false;
}

} // namespace

TEST(wavreader, pcm16_stereo_keeps_its_channels_and_rate)
{
    const auto bytes = riff({{"fmt ", fmtBody(1, 2, 44100, 16)}, {"data", pcm16({16384, -32768, 0, 32767})}});
    const auto wav = decodeWav(bytes, "a.wav");
    CHECK(wav.audio && wav.audio->channels.size() == 2 && wav.audio->frames == 2);
    CHECK(wav.audio->sampleRate == 44100.0 && wav.bitsPerSample == 16 && !wav.isFloat);
    CHECK(wav.audio->path == "a.wav");
    CHECK_NEAR(wav.audio->channels[0][0], 0.5, 1e-6);
    CHECK_NEAR(wav.audio->channels[1][0], -1.0, 1e-6);
    CHECK_NEAR(wav.audio->channels[0][1], 0.0, 1e-6);
    CHECK_NEAR(wav.audio->channels[1][1], 32767.0 / 32768.0, 1e-6);
    CHECK(!wav.hasLoop);
}

TEST(wavreader, pcm8_24_32_and_float_formats_decode)
{
    Bytes d8{0, 128, 255};
    auto w8 = decodeWav(riff({{"fmt ", fmtBody(1, 1, 8000, 8)}, {"data", d8}}), "8.wav");
    CHECK(w8.audio->frames == 3);
    CHECK_NEAR(w8.audio->channels[0][0], -1.0, 1e-6);
    CHECK_NEAR(w8.audio->channels[0][1], 0.0, 1e-6);

    Bytes d24{0x00, 0x00, 0x40, 0x00, 0x00, 0xC0, 0xFF, 0xFF, 0xFF};   // 0.5, -0.5, -1 LSB
    auto w24 = decodeWav(riff({{"fmt ", fmtBody(1, 1, 48000, 24)}, {"data", d24}}), "24.wav");
    CHECK(w24.audio->frames == 3);
    CHECK_NEAR(w24.audio->channels[0][0], 0.5, 1e-7);
    CHECK_NEAR(w24.audio->channels[0][1], -0.5, 1e-7);
    CHECK_NEAR(w24.audio->channels[0][2], -1.0 / 8388608.0, 1e-9);

    Bytes d32;
    put32(d32, 0x40000000u);                                          // 0.5
    auto w32 = decodeWav(riff({{"fmt ", fmtBody(1, 1, 48000, 32)}, {"data", d32}}), "32.wav");
    CHECK_NEAR(w32.audio->channels[0][0], 0.5, 1e-7);

    Bytes f32;
    put32(f32, std::bit_cast<std::uint32_t>(0.25f)); put32(f32, std::bit_cast<std::uint32_t>(-0.75f));
    auto wf = decodeWav(riff({{"fmt ", fmtBody(3, 1, 96000, 32)}, {"data", f32}}), "f.wav");
    CHECK(wf.isFloat && wf.audio->frames == 2 && wf.audio->sampleRate == 96000.0);
    CHECK_NEAR(wf.audio->channels[0][1], -0.75, 1e-7);

    Bytes f64;
    const auto bits = std::bit_cast<std::uint64_t>(0.125);
    put32(f64, std::uint32_t(bits)); put32(f64, std::uint32_t(bits >> 32));
    auto wd = decodeWav(riff({{"fmt ", fmtBody(3, 1, 48000, 64)}, {"data", f64}}), "d.wav");
    CHECK_NEAR(wd.audio->channels[0][0], 0.125, 1e-9);
}

TEST(wavreader, extensible_header_is_read_like_the_plain_one)
{
    auto fmt = fmtBody(0xFFFE, 1, 48000, 24);
    put16(fmt, 22); put16(fmt, 24); put32(fmt, 4);                    // cbSize, valid bits, mask
    put16(fmt, 1);                                                    // PCM sub-format GUID
    const std::uint8_t tail[]{0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    fmt.insert(fmt.end(), std::begin(tail), std::end(tail));
    const auto wav = decodeWav(riff({{"fmt ", fmt}, {"data", Bytes{0x00, 0x00, 0x40}}}), "ext.wav");
    CHECK(wav.audio->frames == 1);
    CHECK_NEAR(wav.audio->channels[0][0], 0.5, 1e-7);

    auto odd = fmt;
    odd[24] = 2;                                                      // ADPCM inside the extensible header
    CHECK(throws(riff({{"fmt ", odd}, {"data", Bytes{0x00, 0x00, 0x40}}})));
}

TEST(wavreader, smpl_loop_and_other_chunks_in_any_order)
{
    Bytes smpl(36, 0);
    smpl[28] = 1;                                                     // one loop
    Bytes loop;
    put32(loop, 0); put32(loop, 0); put32(loop, 1); put32(loop, 3); put32(loop, 0); put32(loop, 0);
    smpl.insert(smpl.end(), loop.begin(), loop.end());
    const auto bytes = riff({{"LIST", Bytes{'I', 'N', 'F'}},          // odd size: padded
                             {"fmt ", fmtBody(1, 1, 48000, 16)},
                             {"data", pcm16({1, 2, 3, 4, 5})},
                             {"smpl", smpl}});
    const auto wav = decodeWav(bytes, "loop.wav");
    CHECK(wav.audio->frames == 5);
    CHECK(wav.hasLoop && wav.loopStart == 1 && wav.loopEnd == 3);

    Bytes far = smpl;                                                 // a loop past the end: ignored
    far[36 + 12] = 9;
    const auto past = decodeWav(riff({{"fmt ", fmtBody(1, 1, 48000, 16)}, {"data", pcm16({1, 2, 3})}, {"smpl", far}}), "p.wav");
    CHECK(!past.hasLoop && past.loopStart == 0 && past.loopEnd == 0);
}

TEST(wavreader, a_data_chunk_longer_than_the_file_gives_the_whole_frames_there)
{
    auto bytes = riff({{"fmt ", fmtBody(1, 2, 48000, 16)}, {"data", pcm16({100, 200, 300, 400, 500, 600})}});
    bytes.resize(bytes.size() - 3);                                   // cut mid-frame
    const auto wav = decodeWav(bytes, "cut.wav");
    CHECK(wav.audio->frames == 2);
    CHECK_NEAR(wav.audio->channels[1][1], 400.0 / 32768.0, 1e-7);
}

TEST(wavreader, bad_files_are_refused_with_the_file_name)
{
    CHECK(throws(Bytes{}));
    CHECK(throws(Bytes{'R', 'I', 'F', 'X', 0, 0, 0, 0, 'W', 'A', 'V', 'E'}));
    CHECK(throws(riff({{"data", pcm16({1})}})));                      // no format
    CHECK(throws(riff({{"fmt ", fmtBody(1, 1, 48000, 16)}})));        // no data
    CHECK(throws(riff({{"fmt ", fmtBody(2, 1, 48000, 16)}, {"data", pcm16({1})}})));   // ADPCM
    CHECK(throws(riff({{"fmt ", fmtBody(1, 3, 48000, 16)}, {"data", pcm16({1, 2, 3})}})));
    CHECK(throws(riff({{"fmt ", fmtBody(1, 1, 48000, 12)}, {"data", pcm16({1})}})));
    CHECK(throws(riff({{"fmt ", fmtBody(1, 1, 500, 16)}, {"data", pcm16({1})}})));
    Bytes nan;
    put32(nan, std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()));
    CHECK(throws(riff({{"fmt ", fmtBody(3, 1, 48000, 32)}, {"data", nan}})));
}

TEST(wavreader, reads_from_disk_and_names_a_missing_file)
{
    const auto dir = std::filesystem::temp_directory_path() / "chaosmaker-wavreader-test";
    std::filesystem::create_directories(dir);
    const auto path = (dir / "tone.wav").string();
    {
        const auto bytes = riff({{"fmt ", fmtBody(1, 1, 22050, 16)}, {"data", pcm16({8192, -8192})}});
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    }
    const auto wav = readWav(path);
    CHECK(wav.audio->frames == 2 && wav.audio->sampleRate == 22050.0);
    CHECK_NEAR(wav.audio->channels[0][1], -0.25, 1e-6);

    bool named = false;
    try { readWav((dir / "nope.wav").string()); } catch (const std::runtime_error& e) {
        named = std::string(e.what()).find("nope.wav") != std::string::npos;
    }
    CHECK(named);
    std::filesystem::remove_all(dir);
}
