#include "testkit.h"
#include "audiofile/flacreader.h"
#include "audiofile/md5.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace audiofile;

namespace {

const std::filesystem::path Here = std::filesystem::path(__FILE__).parent_path();
const std::filesystem::path Data = Here / "data";

std::string hex(const Md5::Digest& d)
{
    std::string out;
    char buf[3];
    for (const auto b : d) { std::snprintf(buf, sizeof buf, "%02x", b); out += buf; }
    return out;
}

std::string md5Of(const std::string& text)
{
    Md5 md5;
    md5.add({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    return hex(md5.finish());
}

// The fixture signal (tests/audiofile/data, made from a WAV of exactly this): a triangle, then
// silence (constant subframes), then noise (hard to predict), then a triangle whose low three
// bits are always zero (wasted bits); 3000 frames each, repeating.
std::int64_t fixtureSample(std::int64_t n, int ch, int bits)
{
    std::int64_t v = 0;
    switch ((n / 3000) % 4) {
    case 0: {
        const std::int64_t t = (n * (3 + ch)) % 2000;
        v = (std::abs(t - 1000) - 500) * 50;
        break;
    }
    case 1: v = 0; break;
    case 2: {
        std::uint32_t h = std::uint32_t(std::uint64_t(n) * 2654435761u + std::uint64_t(ch) * 40503u);
        h ^= h >> 13;
        h = std::uint32_t(std::uint64_t(h) * 0x5bd1e995u);
        h ^= h >> 15;
        v = std::int64_t(h % 40001) - 20000;
        break;
    }
    default: {
        const std::int64_t t = (n * 5) % 1600;
        const std::int64_t s = (std::abs(t - 800) - 400) * 60;
        v = (s >= 0 ? s / 8 : -((-s + 7) / 8)) * 8;               // Python's floor division
        break;
    }
    }
    return bits == 24 ? v * 256 : v;
}

// Every sample of the decoded file equals the fixture signal.
bool matchesFixture(const FlacFile& f, int channels, int bits, double rate, std::int64_t frames)
{
    if (!f.audio || f.audio->frames != frames || int(f.audio->channels.size()) != channels) return false;
    if (f.audio->sampleRate != rate || f.bitsPerSample != bits) return false;
    const double scale = double(std::int64_t(1) << (bits - 1));
    for (int c = 0; c < channels; ++c)
        for (std::int64_t n = 0; n < frames; ++n)
            if (std::llround(double(f.audio->channels[std::size_t(c)][std::size_t(n)]) * scale) != fixtureSample(n, c, bits))
                return false;
    return true;
}

std::vector<std::uint8_t> bytesOf(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

template <typename F>
std::string errorOf(F&& f)
{
    try { f(); } catch (const std::exception& e) { return e.what(); }
    return {};
}

} // namespace

TEST(flacreader, md5_matches_the_rfc_examples)
{
    CHECK(md5Of("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(md5Of("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(md5Of("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
    CHECK(md5Of("12345678901234567890123456789012345678901234567890123456789012345678901234567890")
          == "57edf4a22be3c955ac49da2e2107b67a");
    // Fed in uneven pieces: the same digest.
    const std::string text = "The quick brown fox jumps over the lazy dog, again and again and again.";
    Md5 pieces;
    for (std::size_t i = 0; i < text.size(); i += 7)
        pieces.add({reinterpret_cast<const std::uint8_t*>(text.data()) + i, std::min<std::size_t>(7, text.size() - i)});
    CHECK(hex(pieces.finish()) == md5Of(text));
}

TEST(flacreader, every_stereo_mode_and_prediction_decodes_sample_for_sample)
{
    for (const char* name : {"s16_independent.flac", "s16_left_side.flac", "s16_right_side.flac",
                             "s16_mid_side_lpc32.flac", "s16_fixed_odd_blocks.flac"}) {
        const auto file = readFlac((Data / name).string());
        const bool ok = matchesFixture(file, 2, 16, 44100.0, 12000);
        if (!ok) std::printf("    %s differs\n", name);
        CHECK(ok);
        CHECK(file.md5Checked);
        CHECK(file.audio->path == (Data / name).string());
    }
}

TEST(flacreader, mono_24_bit_decodes_sample_for_sample)
{
    const auto file = readFlac((Data / "s24_mono.flac").string());
    CHECK(matchesFixture(file, 1, 24, 48000.0, 12000));
    CHECK(file.md5Checked);
}

TEST(flacreader, damage_is_reported_not_played)
{
    const auto good = bytesOf(Data / "s16_left_side.flac");
    CHECK(!good.empty());
    const std::string name = "damaged.flac";

    auto flipped = good;                                   // one bit in the middle of the audio
    flipped[flipped.size() / 2] ^= 0x10;
    CHECK(errorOf([&] { decodeFlac(flipped, name); }).find("damaged") != std::string::npos);

    auto cut = good;                                       // the last part missing
    cut.resize(cut.size() * 3 / 4);
    CHECK(!errorOf([&] { decodeFlac(cut, name); }).empty());

    auto wrongMd5 = good;                                  // a valid stream, the wrong MD5
    wrongMd5[4 + 4 + 18] ^= 0xFF;                          // "fLaC", a block header, then MD5 at 18
    CHECK(errorOf([&] { decodeFlac(wrongMd5, name); }).find("MD5") != std::string::npos);

    std::vector<std::uint8_t> wav = {'R', 'I', 'F', 'F', 0, 0, 0, 0};
    CHECK(errorOf([&] { decodeFlac(wav, name); }).find("not a FLAC file") != std::string::npos);
    CHECK(errorOf([&] { readFlac((Data / "missing.flac").string()); }).find("cannot be opened") != std::string::npos);
}

TEST(flacreader, a_leading_id3_tag_is_skipped)
{
    auto bytes = bytesOf(Data / "s24_mono.flac");
    std::vector<std::uint8_t> tagged = {'I', 'D', '3', 4, 0, 0, 0, 0, 0, 5, 1, 2, 3, 4, 5};
    tagged.insert(tagged.end(), bytes.begin(), bytes.end());
    CHECK(matchesFixture(decodeFlac(tagged, "tagged.flac"), 1, 24, 48000.0, 12000));
}

TEST(flacreader, every_splendid_piano_sample_matches_its_own_md5)
{
    // The piano library (not in the repository): when it is here, every file must decode and
    // match the MD5 its encoder stored.
    const auto folder = Here.parent_path().parent_path() / "sample-kits/GrandPiano/SplendidGrandPiano/Samples";
    if (!std::filesystem::is_directory(folder)) {
        std::printf("    skipped: %s is not here\n", folder.string().c_str());
        return;
    }
    int files = 0, checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.path().extension() != ".flac") continue;
        ++files;
        const std::string error = errorOf([&] {
            const auto f = readFlac(entry.path().string());
            if (f.md5Checked && f.audio->frames > 0 && f.audio->sampleRate == 44100.0) ++checked;
        });
        if (!error.empty()) std::printf("    %s\n", error.c_str());
    }
    CHECK(files == 226);
    CHECK(checked == files);
}
