// chordrender: plays a chord progression through the Chords track's player and the grand piano
// offline and writes stereo WAV files, for listening to the autoplay patterns (restMap G4).
//
//   chordrender <out-dir> [chords ...]
//
// For every meter (4/4, 3/4, 6/8, 7/8, 5/4), style (Flowing, Pop, Comping, Ballad) and each of its
// variations, at 100 BPM, it writes <out-dir>/<n>-<d>-<style>-<v>-<variation>.wav: the chords
// (default Am G F E) one per bar, twice
// through, then the last chord stopped and the piano left to ring out. The piano comes from the
// sample-kits folder (or $CHAOSMAKER_SAMPLE_KITS); nothing else is read or written.
#include "instruments/piano/piano.h"
#include "plugins/chordplayer/chordplayer.h"
#include "plugins/chordplayer/chordsymbol.h"
#include "processor/processor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int Rate = 48000;
constexpr int Block = 256;

void writeWav(const std::string& path, const std::vector<float>& left, const std::vector<float>& right)
{
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot write " + path);
    const auto put32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) file.put(char(v >> (8 * i) & 0xFF)); };
    const auto put16 = [&](std::uint16_t v) { file.put(char(v & 0xFF)); file.put(char(v >> 8)); };
    const std::uint32_t dataBytes = std::uint32_t(left.size() * 2 * 4);
    file.write("RIFF", 4); put32(36 + dataBytes); file.write("WAVE", 4);
    file.write("fmt ", 4); put32(16); put16(3); put16(2); put32(Rate);
    put32(Rate * 8); put16(8); put16(32);
    file.write("data", 4); put32(dataBytes);
    for (std::size_t i = 0; i < left.size(); ++i) {
        file.write(reinterpret_cast<const char*>(&left[i]), 4);
        file.write(reinterpret_cast<const char*>(&right[i]), 4);
    }
}

struct Take {
    int numerator, denominator;
    double bpm;
    int mode;
    int variation;
};

// One take: the chords one per bar, twice, then the last one pressed again (it stops) and a
// two-second tail. Returns the peak.
double render(Piano& piano, const std::array<ChordPlayer::Strip, 8>& strips, int chordCount,
              const Take& take, const std::string& path)
{
    ChordPlayer player;
    player.prepare(Rate);
    player.setAutoplay(take.mode);
    player.setVariation(take.variation);
    player.setStrips(strips);
    piano.reset();

    const double barBeats = take.numerator * 4.0 / take.denominator;
    const auto sampleOf = [&](double beat) { return long(std::lround(beat * 60.0 / take.bpm * Rate)); };
    std::vector<std::pair<long, int>> presses;                         // sample, strip
    const int bars = 2 * chordCount;
    for (int bar = 0; bar < bars; ++bar) presses.push_back({sampleOf(bar * barBeats), bar % chordCount});
    const long stop = sampleOf(bars * barBeats);
    presses.push_back({stop, (bars - 1) % chordCount});
    const long total = stop + 2 * Rate;

    std::vector<float> left, right;
    left.reserve(std::size_t(total));
    right.reserve(std::size_t(total));
    std::array<float, Block> l{}, r{};
    for (long position = 0; position < total; position += Block) {
        const int frames = int(std::min<long>(Block, total - position));
        MidiBuffer midi;
        for (const auto& [at, strip] : presses)
            if (at >= position && at < position + frames)
                midi.add({int(at - position), 0x90, std::uint8_t(ChordPlayer::ChordKeys[std::size_t(strip)]), 100});
        l.fill(0); r.fill(0);
        float* channels[]{l.data(), r.data()};
        ProcessContext c;
        c.audio = AudioView{channels, 2, frames};
        c.midi = &midi;
        c.sampleRate = Rate;
        c.samplePosition = position;
        c.bpm = take.bpm;
        c.beat = position * take.bpm / 60.0 / Rate;
        c.beatsPerBar = barBeats;
        c.timeSigNumerator = take.numerator;
        c.timeSigDenominator = take.denominator;
        c.playing = true;
        player.process(c);
        piano.process(c);
        left.insert(left.end(), l.begin(), l.begin() + frames);
        right.insert(right.end(), r.begin(), r.begin() + frames);
    }
    double peak = 0;
    for (std::size_t i = 0; i < left.size(); ++i) peak = std::max({peak, double(std::abs(left[i])), double(std::abs(right[i]))});
    writeWav(path, left, right);
    return peak;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: chordrender <out-dir> [chords ...]   (default chords: Am G F E)\n");
        return 2;
    }
    try {
        const std::filesystem::path out = argv[1];
        std::filesystem::create_directories(out);
        std::vector<std::string> names;
        for (int i = 2; i < argc && names.size() < 8; ++i) names.emplace_back(argv[i]);
        if (names.empty()) names = {"Am", "G", "F", "E"};
        std::array<ChordPlayer::Strip, 8> strips{};
        for (std::size_t i = 0; i < names.size(); ++i) {
            const auto chord = chords::parse(names[i]);
            if (!chord) throw std::runtime_error("not a chord: " + names[i]);
            strips[i] = ChordPlayer::stripOf(*chord);
        }

        const std::string folder = piano::findFolder();
        if (folder.empty()) throw std::runtime_error("no sample-kits folder with the grand piano found");
        auto library = std::make_shared<piano::Library>(folder);
        Piano piano(library);
        piano.prepare(Rate, Block);
        library->waitUntilIdle();
        if (library->state() != piano::Library::State::Ready) throw std::runtime_error("piano: " + library->message());
        {                                                              // let the sampler take its zones
            std::array<float, Block> l{}, r{};
            float* channels[]{l.data(), r.data()};
            MidiBuffer midi;
            ProcessContext c;
            c.audio = AudioView{channels, 2, Block};
            c.midi = &midi;
            c.sampleRate = Rate;
            for (int i = 0; i < 4; ++i) piano.process(c);
        }

        for (auto [n, d] : std::vector<std::pair<int, int>>{{4, 4}, {3, 4}, {6, 8}, {7, 8}, {5, 4}})
            for (int mode = 1; mode <= 4; ++mode)
                for (int variation = 0; variation < chords::Variations; ++variation) {
                    std::string name(chords::variationName(mode, variation));
                    for (auto& c : name) if (c == ' ') c = '-';
                    const std::string file = std::to_string(n) + "-" + std::to_string(d) + "-" + std::string(chords::autoplayName(mode))
                                             + "-" + std::to_string(variation + 1) + "-" + name + ".wav";
                    const double peak = render(piano, strips, int(names.size()), Take{n, d, 100.0, mode, variation}, (out / file).string());
                    std::printf("%-32s peak %6.1f dBFS\n", file.c_str(), 20 * std::log10(std::max(peak, 1e-12)));
                }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "chordrender: %s\n", e.what());
        return 1;
    }
    return 0;
}
