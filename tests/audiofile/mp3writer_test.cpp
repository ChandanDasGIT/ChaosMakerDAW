#include "testkit.h"
#include "audiofile/mp3reader.h"
#include "audiofile/mp3writer.h"

#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

// The MP3 encoder (restMap F2: MP3 export), checked by decoding what it writes with this app's
// decoder (which matches ffmpeg; the encoder's files were also checked with ffmpeg: the same
// samples, the same length).
namespace {
std::vector<float> sine(double hz, double amplitude, double rate, std::size_t frames)
{
    std::vector<float> x(frames);
    for (std::size_t i = 0; i < frames; ++i) x[i] = float(amplitude * std::sin(2 * M_PI * hz * double(i) / rate));
    return x;
}

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

// Signal to noise of `got` against `want`, sample for sample (no shift allowed), in dB.
double snrDb(const std::vector<float>& want, const std::vector<float>& got)
{
    double signal = 0, noise = 0;
    for (std::size_t i = 0; i < want.size() && i < got.size(); ++i) {
        signal += double(want[i]) * want[i];
        noise += (double(want[i]) - got[i]) * (double(want[i]) - got[i]);
    }
    return 10 * std::log10(signal / std::max(noise, 1e-30));
}

audiofile::Mp3File roundTrip(const std::vector<std::vector<float>>& channels, double rate, int kbps)
{
    audiofile::Mp3Options options;
    options.bitrateKbps = kbps;
    const auto bytes = audiofile::encodeMp3(channels, rate, options);
    return audiofile::decodeMp3(bytes, "roundtrip.mp3");
}
}

TEST(mp3writer, stereo_comes_back_at_its_exact_length_and_in_place)
{
    const std::size_t n = 44100 * 2 + 123;   // not a whole number of frames
    const std::vector<std::vector<float>> in{sine(440, 0.5, 44100, n), sine(660, 0.4, 44100, n)};
    const auto mp3 = roundTrip(in, 44100, 320);
    const auto& a = *mp3.audio;
    CHECK(mp3.version == 1 && mp3.bitrateKbps == 320 && !mp3.variableBitrate);
    CHECK(a.sampleRate == 44100.0 && a.channels.size() == 2);
    CHECK(a.frames == std::int64_t(n));                 // gapless: the tag's delay and padding trim exactly
    CHECK(mp3.encoderDelay == 576);
    // Sample for sample, with no shift: the delay is right, and the coding is clean.
    CHECK(snrDb(in[0], a.channels[0]) > 60.0);
    CHECK(snrDb(in[1], a.channels[1]) > 60.0);
    CHECK(std::abs(levelAt(a.channels[0], 44100, 440) - 0.5) < 0.005);
    CHECK(std::abs(levelAt(a.channels[1], 44100, 660) - 0.4) < 0.005);
    CHECK(levelAt(a.channels[0], 44100, 660) < 0.002);   // the channels stay apart (mid / side undone)
}

TEST(mp3writer, mono_and_the_other_mpeg1_rates_keep_their_rate)
{
    for (double rate : {48000.0, 32000.0}) {
        const std::size_t n = std::size_t(rate);
        const auto mp3 = roundTrip({sine(1000, 0.3, rate, n)}, rate, 128);
        CHECK(mp3.audio->sampleRate == rate && mp3.audio->channels.size() == 1);
        CHECK(mp3.audio->frames == std::int64_t(n));
        CHECK(snrDb(sine(1000, 0.3, rate, n), mp3.audio->channels[0]) > 40.0);
    }
}

TEST(mp3writer, other_rates_are_resampled_to_44_1_or_48_khz)
{
    const auto from22 = roundTrip({sine(1000, 0.3, 22050, 22050)}, 22050, 192);
    CHECK(from22.audio->sampleRate == 44100.0);
    CHECK(from22.audio->frames == 44100);
    CHECK(std::abs(levelAt(from22.audio->channels[0], 44100, 1000) - 0.3) < 0.01);

    const auto from96 = roundTrip({sine(1000, 0.3, 96000, 96000)}, 96000, 192);
    CHECK(from96.audio->sampleRate == 48000.0);
    CHECK(from96.audio->frames == 48000);
    CHECK(std::abs(levelAt(from96.audio->channels[0], 48000, 1000) - 0.3) < 0.01);
}

TEST(mp3writer, silence_loud_short_and_empty_inputs_encode)
{
    const auto quiet = roundTrip({std::vector<float>(10000, 0.0f), std::vector<float>(10000, 0.0f)}, 44100, 320);
    CHECK(quiet.audio->frames == 10000);
    float peak = 0;
    for (const auto& c : quiet.audio->channels) for (float v : c) peak = std::max(peak, std::abs(v));
    CHECK(peak < 1e-4f);

    // Above full scale (the master can go over 0 dBFS): coded, not refused or wrapped.
    const auto loud = roundTrip({sine(440, 1.5, 44100, 44100)}, 44100, 320);
    CHECK(std::abs(levelAt(loud.audio->channels[0], 44100, 440) - 1.5) < 0.02);

    const auto one = roundTrip({std::vector<float>(1, 0.5f)}, 44100, 128);
    CHECK(one.audio->frames == 1);

    const auto none = roundTrip({std::vector<float>{}}, 44100, 128);
    CHECK(none.audio->frames == 0);
}

TEST(mp3writer, frames_keep_one_header_shape_and_the_bitrate)
{
    const std::vector<std::vector<float>> in{sine(440, 0.5, 44100, 44100 * 3), sine(300, 0.5, 44100, 44100 * 3)};
    audiofile::Mp3Options options;
    options.bitrateKbps = 192;
    const auto bytes = audiofile::encodeMp3(in, 44100, options);
    // Every frame: sync, MPEG-1 Layer III, the same rate and channel mode (decoders such as
    // ffmpeg sync on a steady header; mid / side changes only the mode extension).
    std::size_t p = 0, frames = 0;
    bool same = true;
    while (p + 4 <= bytes.size()) {
        if (bytes[p] != 0xFF || (bytes[p + 1] & 0xFE) != 0xFA) { same = false; break; }
        if ((bytes[p + 2] & 0x0C) != (bytes[0 + 2] & 0x0C) || (bytes[p + 3] & 0xC0) != (bytes[3] & 0xC0)) same = false;
        const int bitrate = std::array<int, 15>{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}[bytes[p + 2] >> 4];
        if (frames > 0 && bitrate != 192) same = false;
        p += std::size_t(144 * bitrate * 1000 / 44100 + ((bytes[p + 2] >> 1) & 1));
        ++frames;
    }
    CHECK(same && p == bytes.size());
    // The audio frames average the exact bitrate (the padding byte comes in as it should).
    const double audioBytes = double(bytes.size()) - double(144 * 192 * 1000 / 44100);   // less the Info frame
    CHECK(std::abs(audioBytes / double(frames - 1) - 144.0 * 192000.0 / 44100.0) < 1.0);
}

TEST(mp3writer, bad_input_is_refused_with_a_message)
{
    const std::vector<float> x(1000, 0.0f);
    bool threw = false;
    try { audiofile::encodeMp3({x, x, x}, 44100); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    threw = false;
    audiofile::Mp3Options odd;
    odd.bitrateKbps = 100;
    try { audiofile::encodeMp3({x}, 44100, odd); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { audiofile::encodeMp3({x, std::vector<float>(10)}, 44100); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
}
