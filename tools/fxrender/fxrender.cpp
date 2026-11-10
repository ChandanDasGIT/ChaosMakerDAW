// fxrender: runs a dry guitar recording through guitar suite pedals, offline, and writes the
// result as a 32-bit float WAV. For listening tests (plan 6.3); not part of the app. The mixing
// plugins (restMap G7) work the same way as "mix.compressor", "mix.deesser", "mix.reverb" and
// "mix.chaosverb".
//
//   fxrender <in.wav> <out.wav> [--ir <cabinet.wav>] [--rms <dBFS>] <pedal>[:param=value,...] ...
//
//   fxrender dry.wav crunch.wav pedal.amp:drive=18,master=-10 pedal.cabinet --ir cab.wav
//
// The input is mixed to mono and fed to both channels, as the engine does with a guitar input.
// Pedals run in the order given, in 128-frame blocks, at the input file's sample rate.
// --rms scales the output to that RMS level, so renders compare at equal loudness.
// Prints the peak and RMS level of the input and the output.
//
//   fxrender --analyze <a.wav> [<b.wav> ...]
//
// Prints each file's tone balance (average spectrum in third-octave bands, relative to the
// 1 kHz band) and crest factors (peak over RMS: how much picking attack survives), ignoring
// silent parts. Use it to compare renders with a reference recording.

#include "dsp/fft.h"
#include "instruments/guitar/audiofile/impulsewav.h"
#include "instruments/guitar/plugins/cabinet/cabinet.h"
#include "instruments/guitar/plugins/guitarsuite/pedalregistry.h"
#include "plugins/mixfx/mixfxfactory.h"
#include "processor/processor.h"

#include <algorithm>
#include <complex>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Audio {
    std::vector<float> mono;
    int sampleRate = 0;
};

std::uint32_t u32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t(p[3]) << 24); }
std::uint16_t u16(const unsigned char* p) { return std::uint16_t(p[0] | (p[1] << 8)); }

// RIFF WAV: PCM 16/24/32-bit or float32, any channel count (mixed to mono).
Audio readWav(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open " + path);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WAVE", 4))
        throw std::runtime_error(path + " is not a WAV file");
    int format = 0, channels = 0, bits = 0, rate = 0;
    const unsigned char* data = nullptr;
    std::size_t dataSize = 0;
    for (std::size_t pos = 12; pos + 8 <= bytes.size();) {
        const std::uint32_t size = u32(&bytes[pos + 4]);
        const unsigned char* body = &bytes[pos + 8];
        if (pos + 8 + size > bytes.size()) break;
        if (!std::memcmp(&bytes[pos], "fmt ", 4) && size >= 16) {
            format = u16(body);
            channels = u16(body + 2);
            rate = int(u32(body + 4));
            bits = u16(body + 14);
            if (format == 0xFFFE && size >= 26) format = u16(body + 24);   // extensible
        } else if (!std::memcmp(&bytes[pos], "data", 4)) {
            data = body;
            dataSize = size;
        }
        pos += 8 + size + (size & 1);
    }
    if (!data || channels <= 0 || rate <= 0) throw std::runtime_error(path + ": missing fmt or data");
    const int width = bits / 8;
    if (!((format == 1 && (bits == 16 || bits == 24 || bits == 32)) || (format == 3 && bits == 32)))
        throw std::runtime_error(path + ": unsupported sample format");
    const std::size_t frames = dataSize / std::size_t(width * channels);
    Audio audio;
    audio.sampleRate = rate;
    audio.mono.resize(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        double sum = 0.0;
        for (int c = 0; c < channels; ++c) {
            const unsigned char* p = data + (i * std::size_t(channels) + std::size_t(c)) * std::size_t(width);
            double v = 0.0;
            if (format == 3) { float f; std::memcpy(&f, p, 4); v = f; }
            else if (bits == 16) v = std::int16_t(u16(p)) / 32768.0;
            else if (bits == 24) {
                const std::uint32_t raw = std::uint32_t(p[0]) << 8 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 24;
                v = double(std::int32_t(raw) >> 8) / 8388608.0;
            }
            else v = std::int32_t(u32(p)) / 2147483648.0;
            sum += v;
        }
        audio.mono[i] = float(sum / channels);
    }
    return audio;
}

void writeWav(const std::string& path, const std::vector<float>& left, const std::vector<float>& right, int rate)
{
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot write " + path);
    const auto put32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) file.put(char(v >> (8 * i) & 0xFF)); };
    const auto put16 = [&](std::uint16_t v) { file.put(char(v & 0xFF)); file.put(char(v >> 8)); };
    const std::uint32_t dataBytes = std::uint32_t(left.size() * 2 * 4);
    file.write("RIFF", 4); put32(36 + dataBytes); file.write("WAVE", 4);
    file.write("fmt ", 4); put32(16); put16(3); put16(2); put32(std::uint32_t(rate));
    put32(std::uint32_t(rate) * 8); put16(8); put16(32);
    file.write("data", 4); put32(dataBytes);
    for (std::size_t i = 0; i < left.size(); ++i) {
        file.write(reinterpret_cast<const char*>(&left[i]), 4);
        file.write(reinterpret_cast<const char*>(&right[i]), 4);
    }
}

void report(const char* label, const std::vector<float>& x)
{
    double peak = 0.0, sum = 0.0;
    for (float v : x) { peak = std::max(peak, double(std::abs(v))); sum += double(v) * v; }
    const double rms = x.empty() ? 0.0 : std::sqrt(sum / double(x.size()));
    std::printf("%-7s peak %6.1f dBFS   rms %6.1f dBFS\n", label,
                20 * std::log10(std::max(peak, 1e-12)), 20 * std::log10(std::max(rms, 1e-12)));
}

std::shared_ptr<Processor> makePedal(const std::string& spec, const std::string& irPath, int rate)
{
    const auto colon = spec.find(':');
    const std::string type = spec.substr(0, colon);
    std::shared_ptr<Processor> pedal;
    if (type == "pedal.cabinet" && !irPath.empty()) pedal = std::make_shared<Cabinet>(loadImpulseWav(irPath, rate));
    else if (type.rfind("mix.", 0) == 0) pedal = mixfx::create(type.substr(4));   // restMap G7: mix.compressor, ...
    else pedal = PedalRegistry::create(type);
    if (!pedal) throw std::runtime_error("unknown pedal " + type);
    if (colon == std::string::npos) return pedal;
    std::string rest = spec.substr(colon + 1);
    while (!rest.empty()) {
        const auto comma = rest.find(',');
        const std::string pair = rest.substr(0, comma);
        rest = comma == std::string::npos ? "" : rest.substr(comma + 1);
        const auto eq = pair.find('=');
        if (eq == std::string::npos) throw std::runtime_error("expected name=value in " + pair);
        const std::string name = pair.substr(0, eq);
        const float value = std::stof(pair.substr(eq + 1));
        bool found = false;
        for (const auto& p : pedal->parameters())
            if (p->id == name) { p->set(value); found = true; }
        if (!found) {
            std::string known;
            for (const auto& p : pedal->parameters()) known += " " + p->id;
            throw std::runtime_error(type + " has no parameter " + name + " (has:" + known + ")");
        }
    }
    return pedal;
}

// Tone balance and dynamics of one file (mono mix), skipping parts quieter than -50 dBFS.
void analyze(const std::string& path)
{
    const Audio audio = readWav(path);
    const auto& x = audio.mono;
    constexpr int size = 8192;
    Fft fft(size);
    std::vector<std::complex<double>> bins(size);
    std::vector<double> window(size), power(size / 2 + 1, 0.0);
    for (int i = 0; i < size; ++i) window[std::size_t(i)] = 0.5 - 0.5 * std::cos(6.283185307179586 * i / size);
    int frames = 0;
    for (std::size_t start = 0; start + size <= x.size(); start += size / 2) {
        double sum = 0.0;
        for (int i = 0; i < size; ++i) sum += double(x[start + std::size_t(i)]) * x[start + std::size_t(i)];
        if (10 * std::log10(sum / size + 1e-30) < -50.0) continue;
        for (int i = 0; i < size; ++i) bins[std::size_t(i)] = {x[start + std::size_t(i)] * window[std::size_t(i)], 0.0};
        fft.forward(bins.data());
        for (std::size_t k = 0; k < power.size(); ++k) power[k] += std::norm(bins[k]);
        ++frames;
    }
    std::printf("%s\n  %.1f s, %d Hz, %d analysed frames\n", path.c_str(), double(x.size()) / audio.sampleRate,
                audio.sampleRate, frames);
    if (frames == 0) return;
    // Third-octave bands, 31.5 Hz .. 16 kHz, power relative to the 1 kHz band.
    std::vector<double> centres, levels;
    for (int n = -15; n <= 12; ++n) centres.push_back(1000.0 * std::pow(2.0, n / 3.0));
    const double binHz = double(audio.sampleRate) / size;
    for (double centre : centres) {
        const double lo = centre / std::pow(2.0, 1.0 / 6.0), hi = centre * std::pow(2.0, 1.0 / 6.0);
        double sum = 0.0;
        for (std::size_t k = 1; k < power.size(); ++k)
            if (k * binHz >= lo && k * binHz < hi) sum += power[k];
        levels.push_back(10 * std::log10(sum + 1e-30));
    }
    const double ref = levels[15];
    std::printf("  tone balance (dB vs 1 kHz):\n");
    for (std::size_t i = 0; i < centres.size(); ++i) {
        const double rel = levels[i] - ref;
        const int bar = std::clamp(int(std::lround((rel + 40) / 2)), 0, 40);
        std::printf("  %7.0f Hz %6.1f  %s\n", centres[i], rel, std::string(std::size_t(bar), '#').c_str());
    }
    // Crest factor of the whole file, and the median over 50 ms windows that have sound.
    double peak = 0.0, sum = 0.0;
    std::size_t counted = 0;
    std::vector<double> crests;
    const std::size_t win = std::size_t(audio.sampleRate / 20);
    for (std::size_t start = 0; start + win <= x.size(); start += win) {
        double p = 0.0, s2 = 0.0;
        for (std::size_t i = start; i < start + win; ++i) { p = std::max(p, double(std::abs(x[i]))); s2 += double(x[i]) * x[i]; }
        const double rms = std::sqrt(s2 / double(win));
        if (20 * std::log10(rms + 1e-30) < -50.0) continue;
        crests.push_back(20 * std::log10(p / rms));
        peak = std::max(peak, p); sum += s2; counted += win;
    }
    if (counted == 0) return;
    std::sort(crests.begin(), crests.end());
    std::printf("  crest factor: whole %.1f dB, 50 ms median %.1f dB (sine = 3.0, square = 0.0)\n",
                20 * std::log10(peak / std::sqrt(sum / double(counted))), crests[crests.size() / 2]);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc >= 3 && std::string(argv[1]) == "--analyze") {
        try {
            for (int i = 2; i < argc; ++i) analyze(argv[i]);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "fxrender: %s\n", e.what());
            return 1;
        }
        return 0;
    }
    if (argc < 4) {
        std::fprintf(stderr, "usage: fxrender <in.wav> <out.wav> [--ir <cabinet.wav>] [--rms <dBFS>] <pedal>[:param=value,...] ...\n");
        return 2;
    }
    try {
        const Audio input = readWav(argv[1]);
        std::string irPath;
        double targetRms = 1.0;   // 1.0 = leave the level alone
        std::vector<std::string> specs;
        for (int i = 3; i < argc; ++i) {
            if (std::string(argv[i]) == "--ir" && i + 1 < argc) irPath = argv[++i];
            else if (std::string(argv[i]) == "--rms" && i + 1 < argc) targetRms = std::stod(argv[++i]);
            else specs.emplace_back(argv[i]);
        }
        constexpr int block = 128;
        std::vector<std::shared_ptr<Processor>> chain;
        for (const auto& spec : specs) {
            chain.push_back(makePedal(spec, irPath, input.sampleRate));
            chain.back()->prepare(input.sampleRate, block);
            chain.back()->reset();
        }
        std::vector<float> left = input.mono, right = input.mono;
        for (std::size_t done = 0; done < left.size(); done += block) {
            float* channels[2] = {left.data() + done, right.data() + done};
            ProcessContext context;
            context.audio = AudioView{channels, 2, int(std::min<std::size_t>(block, left.size() - done))};
            context.sampleRate = input.sampleRate;
            for (auto& pedal : chain) pedal->process(context);
        }
        if (targetRms <= 0.0) {
            double sum = 0.0;
            for (float v : left) sum += double(v) * v;
            const double rms = std::sqrt(sum / double(std::max<std::size_t>(1, left.size())));
            const double scale = rms > 0.0 ? std::pow(10.0, targetRms / 20.0) / rms : 1.0;
            for (auto* channel : {&left, &right})
                for (float& v : *channel) v = float(v * scale);
        }
        writeWav(argv[2], left, right, input.sampleRate);
        report("input", input.mono);
        report("output", left);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fxrender: %s\n", e.what());
        return 1;
    }
    return 0;
}
