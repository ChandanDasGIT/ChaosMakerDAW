#include "analysis/tonematch.h"

#include "analysis/dryguitar.h"
#include "instruments/guitar/plugins/guitarsuite/pedalregistry.h"
#include "processor/processor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>

namespace analysis {
namespace {

constexpr double Rate = 48000.0;           // the renders' sample rate
constexpr int Block = 128;
// A reference this seldom in chords is single notes.
constexpr double SoloBelowChordShare = 0.2;
// A dB of crest apart counts as this many dB of colour apart.
constexpr double CrestWeight = 0.6;
// The gain is chosen mostly by the crest: the colour is put right afterwards.
constexpr double GainStageBalanceWeight = 0.35;
// A tone control or EQ band at its stop costs this many dB.
constexpr double ExtremeCost = 1.5;
constexpr double EqMaxGain = 9.0;

struct Knobs {
    int channel = 1;
    double drive = 14.0, bass = 0.0, mid = 0.0, treble = 0.0, presence = 0.3, resonance = 0.3;
    bool overdrive = false;
    double odDrive = 12.0, odTone = 0.62;
    int cabinet = 3;
    double eqHz[3] = {150.0, 900.0, 4000.0}, eqGain[3] = {0.0, 0.0, 0.0};
};

using Values = std::vector<std::pair<std::string, double>>;

// The pedalboard for the knobs, in order: {type, the values that differ from the pedal's own}.
std::vector<std::pair<std::string, Values>> boardOf(const Knobs& k)
{
    std::vector<std::pair<std::string, Values>> board;
    if (k.overdrive) board.push_back({"pedal.overdrive", {{"drive", k.odDrive}, {"tone", k.odTone}, {"level", -6.0}}});
    board.push_back({"pedal.amp", {{"channel", double(k.channel)}, {"drive", k.drive}, {"bass", k.bass}, {"mid", k.mid}, {"treble", k.treble},
                                   {"presence", k.presence}, {"resonance", k.resonance}, {"master", -12.0}}});
    board.push_back({"pedal.cabinet", {{"model", double(k.cabinet)}, {"mix", 1.0}, {"level", 0.0}}});
    board.push_back({"pedal.parametriceq", {{"lowfreq", k.eqHz[0]}, {"lowgain", k.eqGain[0]}, {"lowq", 0.7}, {"midfreq", k.eqHz[1]}, {"midgain", k.eqGain[1]},
                                            {"midq", 0.9}, {"highfreq", k.eqHz[2]}, {"highgain", k.eqGain[2]}, {"highq", 0.7}, {"level", 0.0}}});
    return board;
}

std::shared_ptr<Processor> pedalOf(const std::string& type, const Values& values)
{
    auto pedal = PedalRegistry::create(type);
    if (!pedal) return nullptr;
    for (const auto& [id, value] : values)
        for (const auto& p : pedal->parameters())
            if (p->id == id) p->set(float(value));
    return pedal;
}

// The dry guitar through the board; empty when a pedal is missing.
std::vector<float> render(const std::vector<float>& dry, const Knobs& knobs)
{
    std::vector<std::shared_ptr<Processor>> chain;
    for (const auto& [type, values] : boardOf(knobs)) {
        auto pedal = pedalOf(type, values);
        if (!pedal) return {};
        pedal->prepare(Rate, Block);
        pedal->reset();
        chain.push_back(std::move(pedal));
    }
    std::vector<float> left = dry, right = dry;
    for (std::size_t done = 0; done < left.size(); done += Block) {
        float* channels[2] = {left.data() + done, right.data() + done};
        ProcessContext context;
        context.audio = AudioView{channels, 2, int(std::min<std::size_t>(Block, left.size() - done))};
        context.sampleRate = Rate;
        for (auto& pedal : chain) pedal->process(context);
    }
    return left;
}

double sq(double v) { return v * v; }

// What settings far from the middle cost, in dB.
double extremeness(const Knobs& k)
{
    // The drive too: how squashed a sound is depends on the playing as well, and on that alone
    // the search would turn every amp all the way up.
    double sum = sq(k.bass / 12.0) + sq(k.mid / 12.0) + sq(k.treble / 12.0) + sq((k.drive - 16.0) / 24.0);
    for (double gain : k.eqGain) sum += sq(gain / 12.0);
    return ExtremeCost * sum;
}

struct Search {
    const std::vector<float>& dry;
    const ToneFeatures& reference;
    const Progress& progress;
    int planned = 1;
    int renders = 0;
    bool cancelled = false;

    // The measured result of the knobs; not valid when cancelled or silent.
    ToneFeatures measure(const Knobs& knobs)
    {
        if (cancelled || progress.cancelled()) { cancelled = true; return {}; }
        const auto sound = render(dry, knobs);
        ++renders;
        progress.at(std::min(0.99, double(renders) / planned));
        return sound.empty() ? ToneFeatures{} : measureTone(sound, Rate, false);
    }
    double score(const Knobs& knobs, double balanceWeight = 1.0)
    {
        const ToneFeatures out = measure(knobs);
        if (!out.valid) return 1e9;
        return balanceWeight * balanceDistance(reference, out) + CrestWeight * std::abs(reference.crestDb - out.crestDb) + extremeness(knobs);
    }
};

PedalPresetData presetPedal(const std::string& type, const Values& values)
{
    PedalPresetData data;
    data.type = type;
    if (const auto pedal = pedalOf(type, values))
        for (const auto& p : pedal->parameters()) data.parameters[p->id] = p->get();
    return data;
}

} // namespace

const char* toneChannelName(int channel) noexcept
{
    return channel <= 0 ? "Clean" : channel == 1 ? "Crunch" : "High Gain";
}

const char* toneCabinetName(int model) noexcept
{
    return model <= 1 ? "1x12 Open" : model == 2 ? "2x12" : "4x12 Closed";
}

std::optional<ToneMatch> matchTone(const std::vector<float>& mono, double sampleRate, const ToneMatchOptions& options, const Progress& progress)
{
    if (!(sampleRate > 8000.0) || double(mono.size()) < ToneMinSeconds * sampleRate) return std::nullopt;
    ToneMatch match;
    {
        const auto most = std::size_t(ToneMaxSeconds * sampleRate);
        match.reference = mono.size() > most ? measureTone(std::vector<float>(mono.begin(), mono.begin() + std::ptrdiff_t(most)), sampleRate)
                                              : measureTone(mono, sampleRate);
    }
    if (!match.reference.valid || progress.cancelled()) return std::nullopt;
    match.solo = options.playing == TonePlaying::Solo
              || (options.playing == TonePlaying::Auto && match.reference.chordShare < SoloBelowChordShare);
    const std::vector<float> dry = match.solo ? dryGuitarSolo(Rate) : dryGuitarChords(Rate);
    Search search{dry, match.reference, progress};
    search.planned = 30 + 2 * 31 + 3 * 6 + 2;

    // 1. The gain.
    Knobs best;
    double bestScore = 1e18;
    for (int channel = 0; channel < 3; ++channel)
        for (int od = 0; od < (channel == 0 ? 1 : 2); ++od)
            for (double drive : {4.0, 10.0, 16.0, 22.0, 28.0, 34.0}) {
                Knobs k;
                k.channel = channel;
                k.drive = drive;
                k.overdrive = od == 1;
                const double s = search.score(k, GainStageBalanceWeight);
                if (search.cancelled) return std::nullopt;
                if (s < bestScore - 1e-9) { bestScore = s; best = k; }
            }
    bestScore = search.score(best);

    // 2. The tone controls and the cabinet, one at a time, twice round.
    const auto tryValues = [&](auto member, std::initializer_list<double> values) {
        for (double v : values) {
            Knobs k = best;
            if (k.*member == v) continue;
            k.*member = v;
            const double s = search.score(k);
            if (s < bestScore - 0.05) { bestScore = s; best = k; }
        }
    };
    for (int round = 0; round < 2 && !search.cancelled; ++round) {
        for (int model = 1; model <= 3; ++model) {
            Knobs k = best;
            if (k.cabinet == model) continue;
            k.cabinet = model;
            const double s = search.score(k);
            if (s < bestScore - 0.05) { bestScore = s; best = k; }
        }
        tryValues(&Knobs::bass, {-6.0, -3.0, 0.0, 3.0, 6.0});
        tryValues(&Knobs::mid, {-6.0, -3.0, 0.0, 3.0, 6.0});
        tryValues(&Knobs::treble, {-6.0, -3.0, 0.0, 3.0, 6.0});
        tryValues(&Knobs::presence, {0.1, 0.3, 0.5, 0.7});
        tryValues(&Knobs::resonance, {0.1, 0.3, 0.6});
        tryValues(&Knobs::drive, {std::max(0.0, best.drive - 3.0), std::min(40.0, best.drive + 3.0)});
        if (best.overdrive) tryValues(&Knobs::odTone, {0.4, 0.62, 0.85});
    }
    if (search.cancelled) return std::nullopt;

    // 3. The EQ: each band where the colour is furthest off within its reach, by that much.
    {
        const ToneFeatures out = search.measure(best);
        if (search.cancelled || !out.valid) return std::nullopt;
        double meanRef = 0.0, meanOut = 0.0;
        int count = 0;
        for (int b = 0; b < ToneFeatures::Bands; ++b) {
            const double hz = ToneFeatures::bandHz(b);
            if (hz < 99.0 || hz > 6500.0) continue;
            meanRef += match.reference.balanceDb[std::size_t(b)];
            meanOut += out.balanceDb[std::size_t(b)];
            ++count;
        }
        meanRef /= count;
        meanOut /= count;
        // What is missing (+) or too much (-) per band, smoothed over three bands.
        const auto missing = [&](int b) {
            double sum = 0.0;
            int n = 0;
            for (int c = std::max(0, b - 1); c <= std::min(ToneFeatures::Bands - 1, b + 1); ++c, ++n)
                sum += (match.reference.balanceDb[std::size_t(c)] - meanRef) - (out.balanceDb[std::size_t(c)] - meanOut);
            return sum / n;
        };
        constexpr double reach[3][2] = {{90.0, 450.0}, {250.0, 2400.0}, {1600.0, 6500.0}};
        for (int band = 0; band < 3 && !search.cancelled; ++band) {
            int at = -1;
            for (int b = 0; b < ToneFeatures::Bands; ++b) {
                const double hz = ToneFeatures::bandHz(b);
                if (hz < reach[band][0] || hz > reach[band][1]) continue;
                // Not where a band before this one already works.
                bool taken = false;
                for (int earlier = 0; earlier < band; ++earlier)
                    taken = taken || (best.eqGain[earlier] != 0.0 && std::abs(std::log2(hz / best.eqHz[earlier])) < 0.7);
                if (!taken && (at < 0 || std::abs(missing(b)) > std::abs(missing(at)))) at = b;
            }
            if (at < 0 || std::abs(missing(at)) < 1.0) continue;
            const double want = std::clamp(missing(at), -EqMaxGain, EqMaxGain);
            for (double part : {1.0, 0.6}) {
                Knobs k = best;
                k.eqHz[band] = ToneFeatures::bandHz(at);
                k.eqGain[band] = std::round(want * part * 2.0) / 2.0;
                const double s = search.score(k);
                if (s < bestScore - 0.05) { bestScore = s; best = k; }
            }
        }
    }
    if (search.cancelled) return std::nullopt;

    match.matched = search.measure(best);
    if (search.cancelled || !match.matched.valid) return std::nullopt;
    match.matched.chordShare = match.reference.chordShare;
    match.balanceErrorDb = balanceDistance(match.reference, match.matched);
    match.channel = best.channel;
    match.overdrive = best.overdrive;
    match.cabinet = best.cabinet;
    match.renders = search.renders;

    // The pedalboard: a gate in front, the fitted pedals, then the room.
    const double tail = match.reference.tailSeconds;
    match.reverb = tail > 0.35 || (tail < 0.0 && match.solo);
    match.delay = match.solo && tail > 0.5;
    auto& pedals = match.preset.pedals;
    pedals.push_back(presetPedal("pedal.noisegate", {{"threshold", best.channel == 2 ? -50.0 : -56.0}, {"attack", 2.0}, {"release", best.channel == 2 ? 60.0 : 150.0}}));
    for (const auto& [type, values] : boardOf(best)) pedals.push_back(presetPedal(type, values));
    if (match.delay) pedals.push_back(presetPedal("pedal.delay", {{"time", 380.0}, {"feedback", 0.3}, {"mix", 0.18}}));
    if (match.reverb) {
        const double mix = tail < 0.0 ? 0.12 : std::clamp(0.08 + 0.25 * (tail - 0.35), 0.08, 0.3);
        pedals.push_back(presetPedal("pedal.reverb", {{"room", 0.6}, {"damping", 0.3}, {"mix", mix}}));
    }
    progress.at(1.0);
    return match;
}

} // namespace analysis
