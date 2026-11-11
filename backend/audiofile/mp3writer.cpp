#include "audiofile/mp3writer.h"
#include "audiofile/mp3tables.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace audiofile {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr std::array<int, 15> bitratesV1{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
// The encode -> decode chain's own delay (filterbanks and MDCT), in samples. Zeros are put in
// front so that the delay written in the tag is LAME's usual 576.
constexpr int chainDelay = 1057;   // measured: analysis + MDCT + IMDCT + synthesis
constexpr int tagDelay = 576;
constexpr int decoderDelay = 529;  // what decoders add, by convention

// ---- bits -----------------------------------------------------------------------------------

class BitWriter {
public:
    void put(std::uint32_t value, int bits)
    {
        for (int i = bits - 1; i >= 0; --i) {
            if (m_bit == 0) m_bytes.push_back(0);
            if ((value >> i) & 1) m_bytes.back() |= std::uint8_t(0x80 >> m_bit);
            m_bit = (m_bit + 1) & 7;
        }
    }
    std::size_t bits() const { return m_bytes.size() * 8 - (m_bit ? 8 - m_bit : 0); }
    const std::vector<std::uint8_t>& bytes() const { return m_bytes; }

private:
    std::vector<std::uint8_t> m_bytes;
    int m_bit = 0;
};

// ---- Huffman codes, by table number ---------------------------------------------------------

struct Codes {
    struct Table { int size = 0; std::array<std::array<std::uint32_t, 16>, 16> code{}; std::array<std::array<std::uint8_t, 16>, 16> length{}; };
    std::array<Table, 25> big;
    Codes()
    {
        for (int n : {1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 24}) {
            const auto t = mp3tables::huffTable(n);
            big[std::size_t(n)].size = t.size;
            for (int i = 0; i < t.count; ++i) {
                const auto& e = t.entries[i];
                big[std::size_t(n)].code[e.x][e.y] = e.code;
                big[std::size_t(n)].length[e.x][e.y] = e.length;
            }
        }
    }
};

const Codes& codes()
{
    static const Codes instance;
    return instance;
}

// Bits for values [from, to) with big-value table `table` (0: all must be zero).
int pairBits(const int* ix, int from, int to, int table)
{
    if (table == 0) return 0;
    const auto info = mp3tables::bigValueTables[std::size_t(table)];
    const auto& t = codes().big[std::size_t(info.codes)];
    int bits = 0;
    for (int i = from; i < to; i += 2) {
        int x = std::abs(ix[i]), y = std::abs(ix[i + 1]);
        if (x >= 15 && info.linbits) { bits += info.linbits; x = 15; }
        if (y >= 15 && info.linbits) { bits += info.linbits; y = 15; }
        bits += t.length[std::size_t(x)][std::size_t(y)] + (ix[i] != 0) + (ix[i + 1] != 0);
    }
    return bits;
}

// The cheapest table for values [from, to) and its bits.
std::pair<int, int> bestTable(const int* ix, int from, int to)
{
    int maxValue = 0;
    for (int i = from; i < to; ++i) maxValue = std::max(maxValue, std::abs(ix[i]));
    if (maxValue == 0) return {0, 0};
    std::array<int, 4> candidates{-1, -1, -1, -1};
    if (maxValue <= 1) candidates = {1, -1, -1, -1};
    else if (maxValue <= 2) candidates = {2, 3, -1, -1};
    else if (maxValue <= 3) candidates = {5, 6, -1, -1};
    else if (maxValue <= 5) candidates = {7, 8, 9, -1};
    else if (maxValue <= 7) candidates = {10, 11, 12, -1};
    else if (maxValue <= 15) candidates = {13, 15, -1, -1};
    else {
        // A table with linbits: the smallest linbits that reaches the value, in both families.
        int first16 = -1, first24 = -1;
        for (int t = 16; t <= 23; ++t) if (15 + (1 << mp3tables::bigValueTables[std::size_t(t)].linbits) - 1 >= maxValue) { first16 = t; break; }
        for (int t = 24; t <= 31; ++t) if (15 + (1 << mp3tables::bigValueTables[std::size_t(t)].linbits) - 1 >= maxValue) { first24 = t; break; }
        candidates = {first16, first24, -1, -1};
    }
    int best = -1, bestBits = std::numeric_limits<int>::max();
    for (int t : candidates) {
        if (t < 0) continue;
        const int bits = pairBits(ix, from, to, t);
        if (bits < bestBits) { best = t; bestBits = bits; }
    }
    return {best, bestBits};
}

// ---- one granule's coding -------------------------------------------------------------------

struct Coding {
    int bigValues = 0;
    int count1End = 0;                 // lines [bigValues*2, count1End) are quads
    int region0 = 0, region1 = 0;      // region counts (sfbs) as written in the side info
    std::array<int, 3> table{};
    int count1Table = 0;
    int huffmanBits = 0;
};

Coding analyse(const int* ix, const std::array<int, 23>& longStart)
{
    Coding c;
    int last = 576;
    while (last > 0 && ix[last - 1] == 0) --last;
    last += last & 1;
    int big = last;
    while (big >= 4 && std::abs(ix[big - 1]) <= 1 && std::abs(ix[big - 2]) <= 1 && std::abs(ix[big - 3]) <= 1 && std::abs(ix[big - 4]) <= 1) big -= 4;
    big -= big & 1;
    c.bigValues = big / 2;
    c.count1End = std::min(576, big + ((last - big + 3) / 4) * 4);
    // Region split at band edges near a third and two thirds of the big values.
    int r0 = 0;
    while (r0 < 21 && longStart[std::size_t(r0 + 1)] < big / 3) ++r0;
    r0 = std::min(r0, 15);
    int r1 = r0 + 1;
    while (r1 < 21 && longStart[std::size_t(r1 + 1)] < (2 * big) / 3) ++r1;
    c.region0 = r0;
    c.region1 = std::clamp(r1 - r0 - 1, 0, 7);
    const int start1 = std::min(big, int(longStart[std::size_t(std::min(22, c.region0 + 1))]));
    const int start2 = std::min(big, int(longStart[std::size_t(std::min(22, c.region0 + c.region1 + 2))]));
    int bits = 0;
    const std::array<std::pair<int, int>, 3> regions{{{0, start1}, {start1, start2}, {start2, big}}};
    for (std::size_t r = 0; r < 3; ++r) {
        const auto [t, b] = bestTable(ix, regions[r].first, regions[r].second);
        c.table[r] = t;
        bits += b;
    }
    int bitsA = 0, bitsB = 0;
    for (int i = big; i < c.count1End; i += 4) {
        const int v = (std::abs(ix[i]) << 3) | (std::abs(ix[i + 1]) << 2) | (std::abs(ix[i + 2]) << 1) | std::abs(ix[i + 3]);
        const int signs = (ix[i] != 0) + (ix[i + 1] != 0) + (ix[i + 2] != 0) + (ix[i + 3] != 0);
        bitsA += mp3tables::quadALengths[std::size_t(v)] + signs;
        bitsB += 4 + signs;
    }
    c.count1Table = bitsB < bitsA ? 1 : 0;
    c.huffmanBits = bits + std::min(bitsA, bitsB);
    return c;
}

void writeHuffman(BitWriter& out, const int* ix, const Coding& c, const std::array<int, 23>& longStart)
{
    const int big = c.bigValues * 2;
    const int start1 = std::min(big, int(longStart[std::size_t(std::min(22, c.region0 + 1))]));
    const int start2 = std::min(big, int(longStart[std::size_t(std::min(22, c.region0 + c.region1 + 2))]));
    for (int i = 0; i < big; i += 2) {
        const int table = c.table[std::size_t(i < start1 ? 0 : i < start2 ? 1 : 2)];
        if (table == 0) continue;
        const auto info = mp3tables::bigValueTables[std::size_t(table)];
        const auto& t = codes().big[std::size_t(info.codes)];
        const int x = std::abs(ix[i]), y = std::abs(ix[i + 1]);
        const int cx = info.linbits ? std::min(x, 15) : x, cy = info.linbits ? std::min(y, 15) : y;
        out.put(t.code[std::size_t(cx)][std::size_t(cy)], t.length[std::size_t(cx)][std::size_t(cy)]);
        if (info.linbits && x >= 15) out.put(std::uint32_t(x - 15), info.linbits);
        if (x) out.put(ix[i] < 0, 1);
        if (info.linbits && y >= 15) out.put(std::uint32_t(y - 15), info.linbits);
        if (y) out.put(ix[i + 1] < 0, 1);
    }
    for (int i = big; i < c.count1End; i += 4) {
        const int v = (std::abs(ix[i]) << 3) | (std::abs(ix[i + 1]) << 2) | (std::abs(ix[i + 2]) << 1) | std::abs(ix[i + 3]);
        if (c.count1Table) out.put(std::uint32_t(15 - v), 4);
        else out.put(mp3tables::quadACodes[std::size_t(v)], mp3tables::quadALengths[std::size_t(v)]);
        for (int k = 0; k < 4; ++k) if (ix[i + k]) out.put(ix[i + k] < 0, 1);
    }
}

// ---- the analysis side ----------------------------------------------------------------------

struct Tables {
    std::array<double, 512> c{};
    std::array<std::array<double, 64>, 32> m{};
    std::array<std::array<double, 36>, 18> mdct{};   // [k][n], with the sine window
    std::array<double, 8> cs{}, ca{};
    std::vector<double> pow34;   // not used for large values; quantize() computes those
    Tables()
    {
        for (int i = 0; i < 512; ++i) c[std::size_t(i)] = mp3tables::synthesisWindow(i) / 32.0;
        for (int i = 0; i < 32; ++i)
            for (int k = 0; k < 64; ++k) m[std::size_t(i)][std::size_t(k)] = std::cos((2 * i + 1) * (k - 16) * pi / 64.0);
        for (int k = 0; k < 18; ++k)
            for (int n = 0; n < 36; ++n)
                mdct[std::size_t(k)][std::size_t(n)] = std::sin(pi / 36.0 * (n + 0.5)) * std::cos(pi / 72.0 * (2 * n + 1 + 18) * (2 * k + 1));
        const double coef[8]{-0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037};
        for (int i = 0; i < 8; ++i) {
            const double root = std::sqrt(1.0 + coef[i] * coef[i]);
            cs[std::size_t(i)] = 1.0 / root;
            ca[std::size_t(i)] = coef[i] / root;
        }
    }
};

const Tables& tables()
{
    static const Tables instance;
    return instance;
}

struct ChannelState {
    std::array<double, 512> x{};                       // the analysis filterbank's input history
    std::array<std::array<double, 18>, 32> previous{}; // last granule's subband samples
};

// 576 new samples -> 576 MDCT lines (long blocks).
void analyse(const float* samples, ChannelState& state, std::array<double, 576>& xr)
{
    const auto& t = tables();
    std::array<std::array<double, 18>, 32> subbands{};
    for (int slot = 0; slot < 18; ++slot) {
        std::memmove(&state.x[32], &state.x[0], sizeof(double) * (512 - 32));
        for (int i = 0; i < 32; ++i) state.x[std::size_t(31 - i)] = samples[slot * 32 + i];
        std::array<double, 64> y{};
        for (int i = 0; i < 64; ++i) {
            double sum = 0.0;
            for (int j = 0; j < 8; ++j) sum += t.c[std::size_t(i + 64 * j)] * state.x[std::size_t(i + 64 * j)];
            y[std::size_t(i)] = sum;
        }
        for (int sb = 0; sb < 32; ++sb) {
            double sum = 0.0;
            for (int k = 0; k < 64; ++k) sum += t.m[std::size_t(sb)][std::size_t(k)] * y[std::size_t(k)];
            subbands[std::size_t(sb)][std::size_t(slot)] = sum;
        }
    }
    for (int sb = 0; sb < 32; ++sb) {
        if (sb & 1)   // frequency inversion, matching the decoder's
            for (int i = 1; i < 18; i += 2) subbands[std::size_t(sb)][std::size_t(i)] = -subbands[std::size_t(sb)][std::size_t(i)];
        std::array<double, 36> z{};
        for (int i = 0; i < 18; ++i) {
            z[std::size_t(i)] = state.previous[std::size_t(sb)][std::size_t(i)];
            z[std::size_t(i + 18)] = subbands[std::size_t(sb)][std::size_t(i)];
        }
        state.previous[std::size_t(sb)] = subbands[std::size_t(sb)];
        for (int k = 0; k < 18; ++k) {
            double sum = 0.0;
            for (int n = 0; n < 36; ++n) sum += t.mdct[std::size_t(k)][std::size_t(n)] * z[std::size_t(n)];
            xr[std::size_t(sb * 18 + k)] = sum / 9.0;
        }
    }
    // Alias butterflies: the inverse of the decoder's.
    for (int sb = 1; sb < 32; ++sb)
        for (int i = 0; i < 8; ++i) {
            const std::size_t a = std::size_t((sb - 1) * 18 + 17 - i), b = std::size_t(sb * 18 + i);
            const double lo = xr[a], hi = xr[b];
            xr[a] = lo * t.cs[std::size_t(i)] + hi * t.ca[std::size_t(i)];
            xr[b] = hi * t.cs[std::size_t(i)] - lo * t.ca[std::size_t(i)];
        }
}

// ---- psychoacoustics ------------------------------------------------------------------------

// The allowed noise energy per long band: the band energies spread to their neighbours (a
// simple masking curve on the Bark scale), lowered by a safety offset, never below the
// threshold of hearing.
struct Psy {
    std::array<double, 22> bark{};       // each band's centre on the Bark scale
    std::array<double, 22> quiet{};      // the threshold of hearing per band, as an energy
    explicit Psy(const std::array<int, 23>& longStart, double rate, double fullScaleLineEnergy)
    {
        for (int b = 0; b < 22; ++b) {
            const double centre = (longStart[std::size_t(b)] + longStart[std::size_t(b + 1)]) * 0.5 * rate / 1152.0;
            const double f = std::max(centre, 20.0) / 1000.0;
            bark[std::size_t(b)] = 13.0 * std::atan(0.76 * f) + 3.5 * std::atan((f / 7.5) * (f / 7.5));
            // Terhardt's threshold in dB SPL; full scale taken as 96 dB SPL.
            const double athDb = 3.64 * std::pow(f, -0.8) - 6.5 * std::exp(-0.6 * (f - 3.3) * (f - 3.3)) + 1e-3 * std::pow(f, 4);
            const double lines = longStart[std::size_t(b + 1)] - longStart[std::size_t(b)];
            quiet[std::size_t(b)] = fullScaleLineEnergy * std::pow(10.0, (std::min(athDb, 96.0) - 96.0) / 10.0) * lines;
        }
    }
    std::array<double, 22> allowed(const std::array<double, 576>& xr, const std::array<int, 23>& longStart, double offsetDb) const
    {
        std::array<double, 22> energy{}, result{};
        for (int b = 0; b < 22; ++b)
            for (int i = longStart[std::size_t(b)]; i < longStart[std::size_t(b + 1)]; ++i) energy[std::size_t(b)] += xr[std::size_t(i)] * xr[std::size_t(i)];
        const double offset = std::pow(10.0, -offsetDb / 10.0);
        for (int b = 0; b < 22; ++b) {
            double spread = 0.0;
            for (int j = 0; j < 22; ++j) {
                const double dz = bark[std::size_t(b)] - bark[std::size_t(j)];   // masker j below (dz > 0) or above
                const double db = dz >= 0 ? -25.0 * dz : 10.0 * dz;              // slopes of the masking curve
                if (db > -60.0) spread += energy[std::size_t(j)] * std::pow(10.0, db / 10.0);
            }
            result[std::size_t(b)] = std::max(spread * offset, quiet[std::size_t(b)]);
        }
        return result;
    }
};

// ---- quantization ---------------------------------------------------------------------------

struct Quantized {
    std::array<int, 576> ix{};
    std::array<int, 22> sf{};
    int globalGain = 210;
    int sfCompress = 0;
    Coding coding;
    int part23 = 0;
};

constexpr std::array<int, 16> slen1Of{0, 0, 0, 0, 3, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4};
constexpr std::array<int, 16> slen2Of{0, 1, 2, 3, 0, 1, 2, 3, 1, 2, 3, 1, 2, 3, 2, 3};

// The cheapest scalefac_compress that holds the scale factors, and its part2 bits; -1 if none.
std::pair<int, int> scaleFactorCoding(const std::array<int, 22>& sf)
{
    int max1 = 0, max2 = 0;
    for (int b = 0; b < 11; ++b) max1 = std::max(max1, sf[std::size_t(b)]);
    for (int b = 11; b < 21; ++b) max2 = std::max(max2, sf[std::size_t(b)]);
    int best = -1, bestBits = std::numeric_limits<int>::max();
    for (int k = 0; k < 16; ++k) {
        if ((1 << slen1Of[std::size_t(k)]) <= max1 || (1 << slen2Of[std::size_t(k)]) <= max2) continue;
        const int bits = 11 * slen1Of[std::size_t(k)] + 10 * slen2Of[std::size_t(k)];
        if (bits < bestBits) { best = k; bestBits = bits; }
    }
    return {best, best < 0 ? 0 : bestBits};
}

// Quantizes with a global gain; false when a value is too large for the tables.
bool quantize(const std::array<double, 576>& xr, const std::array<int, 22>& sf, int gain,
              const std::array<int, 23>& longStart, std::array<int, 576>& ix)
{
    const double step = std::pow(2.0, -0.1875 * (gain - 210));   // (2^(-0.25 (g - 210)))^(3/4)
    for (int b = 0; b < 22; ++b) {
        const double amp = std::pow(2.0, 0.375 * sf[std::size_t(b)]);   // (2^(0.5 sf))^(3/4)
        for (int i = longStart[std::size_t(b)]; i < longStart[std::size_t(b + 1)]; ++i) {
            const double a = std::abs(xr[std::size_t(i)]);
            const double v = std::pow(a, 0.75) * amp * step - 0.0946;
            if (v > 8206.0) return false;
            const int q = v > 0 ? int(v + 0.5) : 0;
            ix[std::size_t(i)] = xr[std::size_t(i)] < 0 ? -q : q;
        }
    }
    return true;
}

// The global gain: the smallest (finest) that fits `bits` with these scale factors.
bool innerLoop(const std::array<double, 576>& xr, Quantized& q, int bits, const std::array<int, 23>& longStart)
{
    const auto [sfc, part2] = scaleFactorCoding(q.sf);
    if (sfc < 0) return false;
    int lo = 0, hi = 255;
    bool found = false;
    std::array<int, 576> ix{};
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        Coding c;
        const bool ok = quantize(xr, q.sf, mid, longStart, ix);
        if (ok) c = analyse(ix.data(), longStart);
        if (ok && c.huffmanBits + part2 <= bits && c.huffmanBits + part2 <= 4095) {
            found = true;
            q.ix = ix; q.globalGain = mid; q.coding = c; q.sfCompress = sfc; q.part23 = c.huffmanBits + part2;
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    return found;
}

// Noise per band of a quantization (energy).
std::array<double, 22> bandNoise(const std::array<double, 576>& xr, const Quantized& q, const std::array<int, 23>& longStart)
{
    std::array<double, 22> noise{};
    const double step = std::pow(2.0, 0.25 * (q.globalGain - 210));
    for (int b = 0; b < 22; ++b) {
        const double unamp = std::pow(2.0, -0.5 * q.sf[std::size_t(b)]);
        for (int i = longStart[std::size_t(b)]; i < longStart[std::size_t(b + 1)]; ++i) {
            const double back = std::pow(double(std::abs(q.ix[std::size_t(i)])), 4.0 / 3.0) * step * unamp;
            const double d = std::abs(xr[std::size_t(i)]) - back;
            noise[std::size_t(b)] += d * d;
        }
    }
    return noise;
}

// The outer loop: amplify bands whose noise is above what they may have, while bits allow.
Quantized quantizeGranule(const std::array<double, 576>& xr, const std::array<double, 22>& allowed, int bits,
                          const std::array<int, 23>& longStart)
{
    Quantized best;
    if (!innerLoop(xr, best, bits, longStart)) {   // even the coarsest gain does not fit: silence
        best = Quantized{};
        best.coding = analyse(best.ix.data(), longStart);
        return best;
    }
    double bestOver = std::numeric_limits<double>::max();
    Quantized current = best;
    for (int round = 0; round < 12; ++round) {
        const auto noise = bandNoise(xr, current, longStart);
        double over = 0.0;
        std::array<bool, 22> amplify{};
        for (int b = 0; b < 21; ++b)
            if (noise[std::size_t(b)] > allowed[std::size_t(b)]) {
                over += std::log10(noise[std::size_t(b)] / allowed[std::size_t(b)]);
                amplify[std::size_t(b)] = true;
            }
        if (over < bestOver) { bestOver = over; best = current; }
        if (over <= 0.0) break;
        Quantized next = current;
        bool changed = false;
        for (int b = 0; b < 21; ++b)
            if (amplify[std::size_t(b)] && next.sf[std::size_t(b)] < (b < 11 ? 15 : 7)) { ++next.sf[std::size_t(b)]; changed = true; }
        if (!changed || !innerLoop(xr, next, bits, longStart)) break;
        current = next;
    }
    return best;
}

// ---- resampling (rates MP3 does not have) ---------------------------------------------------

std::vector<float> resample(const std::vector<float>& in, double from, double to)
{
    const double ratio = to / from;
    const std::size_t outLength = std::size_t(std::llround(double(in.size()) * ratio));
    std::vector<float> out(outLength);
    const double cutoff = std::min(1.0, ratio) * 0.95;   // below the lower Nyquist
    constexpr int halfTaps = 32;
    for (std::size_t n = 0; n < outLength; ++n) {
        const double centre = double(n) / ratio;
        const long first = long(std::floor(centre)) - halfTaps + 1;
        double sum = 0.0;
        for (long k = first; k < first + 2 * halfTaps; ++k) {
            if (k < 0 || std::size_t(k) >= in.size()) continue;
            const double x = (double(k) - centre) * cutoff;
            const double sinc = std::abs(x) < 1e-9 ? 1.0 : std::sin(pi * x) / (pi * x);
            const double w = 0.42 + 0.5 * std::cos(pi * (double(k) - centre) / halfTaps)
                           + 0.08 * std::cos(2 * pi * (double(k) - centre) / halfTaps);   // Blackman
            sum += in[std::size_t(k)] * sinc * cutoff * std::max(0.0, w);
        }
        out[n] = float(sum);
    }
    return out;
}

std::uint16_t crc16(const std::uint8_t* data, std::size_t n)
{
    std::uint16_t crc = 0;   // CRC-16/ARC, as the LAME tag uses
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc & 1) ? std::uint16_t((crc >> 1) ^ 0xA001) : std::uint16_t(crc >> 1);
    }
    return crc;
}

} // namespace

std::vector<std::uint8_t> encodeMp3(const std::vector<std::vector<float>>& input, double inputRate, const Mp3Options& options)
{
    if (input.empty() || input.size() > 2) throw std::runtime_error("An MP3 export needs one or two channels.");
    for (const auto& c : input) if (c.size() != input.front().size()) throw std::runtime_error("Channels of different lengths.");
    if (!std::isfinite(inputRate) || inputRate < 8000 || inputRate > 192000) throw std::runtime_error("Unsupported sample rate for MP3.");
    int bitrateIndex = 0;
    for (int i = 1; i < 15; ++i) if (bitratesV1[std::size_t(i)] == options.bitrateKbps) bitrateIndex = i;
    if (!bitrateIndex) throw std::runtime_error("Unsupported MP3 bitrate: " + std::to_string(options.bitrateKbps) + " kbps.");

    // The rate: 32 / 44.1 / 48 kHz as they are, others resampled.
    int rateIndex = -1;
    if (std::abs(inputRate - 44100) < 0.5) rateIndex = 0;
    else if (std::abs(inputRate - 48000) < 0.5) rateIndex = 1;
    else if (std::abs(inputRate - 32000) < 0.5) rateIndex = 2;
    std::vector<std::vector<float>> pcm;
    double rate = inputRate;
    if (rateIndex < 0) {
        rate = (std::fmod(inputRate, 11025.0) == 0.0) ? 44100.0 : 48000.0;
        rateIndex = rate == 44100.0 ? 0 : 1;
        for (const auto& c : input) pcm.push_back(resample(c, inputRate, rate));
    } else {
        pcm = input;
    }
    const int channels = int(pcm.size());
    const std::size_t samples = pcm.front().size();

    // Zeros in front so the tag's delay is 576, then whole frames.
    const int prepend = tagDelay + decoderDelay - chainDelay;
    const std::size_t total = std::size_t(prepend) + samples;
    const std::size_t frameCount = (total + std::size_t(chainDelay - decoderDelay - tagDelay + prepend) + 1151) / 1152 + 1;
    std::vector<std::vector<float>> work(std::size_t(channels), std::vector<float>(frameCount * 1152 + 1152, 0.0f));
    for (int c = 0; c < channels; ++c)
        std::copy(pcm[std::size_t(c)].begin(), pcm[std::size_t(c)].end(), work[std::size_t(c)].begin() + prepend);
    const int padding = int(frameCount * 1152) - int(samples) - tagDelay;

    const int sampleRate = int(rate);
    std::array<int, 23> longStart{};
    for (int i = 0; i < 22; ++i) longStart[std::size_t(i + 1)] = longStart[std::size_t(i)] + mp3tables::longBands[std::size_t(rateIndex)][std::size_t(i)];
    // Full scale, for the threshold of hearing: a full-scale sine's strongest MDCT line.
    double fullScale = 0.0;
    {
        ChannelState probe;
        std::vector<float> sine(576 * 4);
        for (std::size_t i = 0; i < sine.size(); ++i) sine[i] = float(std::sin(2 * pi * 1000.0 * double(i) / rate));
        std::array<double, 576> xr{};
        for (int g = 0; g < 4; ++g) analyse(&sine[std::size_t(g) * 576], probe, xr);
        for (double v : xr) fullScale = std::max(fullScale, v * v);
    }
    const Psy psy(longStart, rate, fullScale);
    // Above the low-pass nothing is coded (bits go where they are heard).
    const double lowpassHz = options.bitrateKbps >= 256 ? 20000 : options.bitrateKbps >= 192 ? 18500
                           : options.bitrateKbps >= 128 ? 16500 : options.bitrateKbps >= 96 ? 15000 : 11000;
    const int lowpassLine = std::min(576, int(lowpassHz / (rate / 2.0) * 576.0));
    const double offsetDb = options.bitrateKbps >= 256 ? 24.0 : options.bitrateKbps >= 160 ? 18.0 : 12.0;

    const int sideInfoBytes = channels == 1 ? 17 : 32;
    std::vector<std::uint8_t> file;
    std::vector<ChannelState> state(static_cast<std::size_t>(channels));
    // The main data stream: all frames' main data slots in order; frames point back into it.
    std::vector<std::size_t> slotStart;   // file offset of each audio frame's main data slots
    std::vector<std::size_t> slotBytes;
    std::size_t slotsUsed = 0;            // bytes of slot space filled so far (logical)
    std::size_t slotsBefore = 0;          // slot space in the frames already written
    double padAccumulator = 0.0;
    const double exactBytes = 144.0 * options.bitrateKbps * 1000.0 / sampleRate;

    // The Info frame (no audio): written first, filled in at the end.
    int infoIndex = bitrateIndex;
    while (infoIndex < 14 && 144 * bitratesV1[std::size_t(infoIndex)] * 1000 / sampleRate < 200) ++infoIndex;
    const std::size_t infoBytes = std::size_t(144 * bitratesV1[std::size_t(infoIndex)] * 1000 / sampleRate);
    file.resize(infoBytes, 0);

    std::array<std::array<double, 576>, 2> xr{};
    for (std::size_t f = 0; f < frameCount; ++f) {
        // This frame's size: the exact bitrate on average, with the padding byte.
        const int baseBytes = int(exactBytes);
        padAccumulator += exactBytes - baseBytes;
        const bool pad = padAccumulator >= 1.0;
        if (pad) padAccumulator -= 1.0;
        const int frameBytes = baseBytes + (pad ? 1 : 0);
        const std::size_t frameStart = file.size();
        file.resize(frameStart + std::size_t(frameBytes), 0);
        const std::size_t mySlots = std::size_t(frameBytes - 4 - sideInfoBytes);
        slotStart.push_back(frameStart + 4 + std::size_t(sideInfoBytes));
        slotBytes.push_back(mySlots);
        // The reservoir: free slot space in earlier frames (at most 511 bytes back).
        std::size_t free = slotsBefore - slotsUsed;
        if (free > 511) { slotsUsed = slotsBefore - 511; free = 511; }
        const int mainDataBegin = int(free);
        int budget = int((free + mySlots) * 8);

        // Analysis and mid / side.
        std::array<std::array<std::array<double, 576>, 2>, 2> granules{};
        for (int gr = 0; gr < 2; ++gr)
            for (int c = 0; c < channels; ++c) {
                analyse(&work[std::size_t(c)][f * 1152 + std::size_t(gr) * 576], state[std::size_t(c)], xr[std::size_t(c)]);
                for (int i = lowpassLine; i < 576; ++i) xr[std::size_t(c)][std::size_t(i)] = 0.0;
                granules[std::size_t(gr)][std::size_t(c)] = xr[std::size_t(c)];
            }
        bool midSide = false;
        if (channels == 2) {
            double side = 0.0, mid = 0.0;
            for (int gr = 0; gr < 2; ++gr)
                for (int i = 0; i < 576; ++i) {
                    const double l = granules[std::size_t(gr)][0][std::size_t(i)], r = granules[std::size_t(gr)][1][std::size_t(i)];
                    mid += (l + r) * (l + r);
                    side += (l - r) * (l - r);
                }
            midSide = side < 0.3 * mid;
            if (midSide) {
                constexpr double invRoot2 = 0.70710678118654752440;
                for (int gr = 0; gr < 2; ++gr)
                    for (int i = 0; i < 576; ++i) {
                        auto& l = granules[std::size_t(gr)][0][std::size_t(i)];
                        auto& r = granules[std::size_t(gr)][1][std::size_t(i)];
                        const double m = (l + r) * invRoot2, s = (l - r) * invRoot2;
                        l = m; r = s;
                    }
            }
        }

        // Quantize: each granule and channel in turn gets its share of what is left.
        std::array<std::array<Quantized, 2>, 2> q;
        int remaining = budget;
        for (int gr = 0; gr < 2; ++gr)
            for (int c = 0; c < channels; ++c) {
                const int parts = (2 - gr) * channels - c;   // granule-channels still to code, this one included
                const int share = std::min(4095, remaining / std::max(1, parts));
                const auto allowed = psy.allowed(granules[std::size_t(gr)][std::size_t(c)], longStart, offsetDb);
                q[std::size_t(gr)][std::size_t(c)] = quantizeGranule(granules[std::size_t(gr)][std::size_t(c)], allowed, share, longStart);
                remaining -= q[std::size_t(gr)][std::size_t(c)].part23;
            }

        // Main data: scale factors and Huffman codes.
        BitWriter main;
        for (int gr = 0; gr < 2; ++gr)
            for (int c = 0; c < channels; ++c) {
                const auto& g = q[std::size_t(gr)][std::size_t(c)];
                const int s1 = slen1Of[std::size_t(g.sfCompress)], s2 = slen2Of[std::size_t(g.sfCompress)];
                for (int b = 0; b < 11; ++b) main.put(std::uint32_t(g.sf[std::size_t(b)]), s1);
                for (int b = 11; b < 21; ++b) main.put(std::uint32_t(g.sf[std::size_t(b)]), s2);
                writeHuffman(main, g.ix.data(), g.coding, longStart);
            }
        // Into the slot space, starting mainDataBegin bytes before this frame's slots.
        std::size_t logical = slotsUsed;
        std::size_t frameIndex = slotStart.size() - 1, slotIndex = 0, offsetInSlot = 0;
        {   // find where `logical` lands
            std::size_t acc = slotsBefore + mySlots;
            frameIndex = slotStart.size() - 1;
            std::size_t frameLogicalStart = slotsBefore;
            while (frameLogicalStart > logical && frameIndex > 0) {
                --frameIndex;
                frameLogicalStart -= slotBytes[frameIndex];
            }
            slotIndex = frameIndex;
            offsetInSlot = logical - frameLogicalStart;
            (void)acc;
        }
        for (std::uint8_t byte : main.bytes()) {
            while (offsetInSlot >= slotBytes[slotIndex]) { ++slotIndex; offsetInSlot = 0; }
            file[slotStart[slotIndex] + offsetInSlot] = byte;
            ++offsetInSlot;
        }
        slotsUsed = logical + main.bytes().size();
        slotsBefore += mySlots;

        // Header and side info.
        BitWriter head;
        head.put(0xFFF, 12); head.put(1, 1); head.put(1, 2); head.put(1, 1);   // MPEG-1, Layer III, no CRC
        head.put(std::uint32_t(bitrateIndex), 4); head.put(std::uint32_t(rateIndex), 2); head.put(pad, 1); head.put(0, 1);
        head.put(channels == 1 ? 3 : 1, 2);   // always joint stereo (decoders sync on a steady mode); M/S per frame below
        head.put(midSide ? 2 : 0, 2);
        head.put(0, 1); head.put(1, 1); head.put(0, 2);                        // not copyrighted, original, no emphasis
        head.put(std::uint32_t(mainDataBegin), 9);
        head.put(0, channels == 1 ? 5 : 3);
        for (int c = 0; c < channels; ++c) head.put(0, 4);                    // no scfsi
        for (int gr = 0; gr < 2; ++gr)
            for (int c = 0; c < channels; ++c) {
                const auto& g = q[std::size_t(gr)][std::size_t(c)];
                head.put(std::uint32_t(g.part23), 12);
                head.put(std::uint32_t(g.coding.bigValues), 9);
                head.put(std::uint32_t(g.globalGain), 8);
                head.put(std::uint32_t(g.sfCompress), 4);
                head.put(0, 1);                                                // long blocks
                for (int r = 0; r < 3; ++r) head.put(std::uint32_t(g.coding.table[std::size_t(r)]), 5);
                head.put(std::uint32_t(g.coding.region0), 4);
                head.put(std::uint32_t(g.coding.region1), 3);
                head.put(0, 1);                                                // no preflag
                head.put(0, 1);                                                // scalefac_scale 0
                head.put(std::uint32_t(g.coding.count1Table), 1);
            }
        std::copy(head.bytes().begin(), head.bytes().end(), file.begin() + std::ptrdiff_t(frameStart));
    }

    // The Info frame: a silent frame carrying the frame count, the size and the LAME-style tag
    // with the delay and padding (decoders trim them: gapless).
    {
        BitWriter head;
        head.put(0xFFF, 12); head.put(1, 1); head.put(1, 2); head.put(1, 1);
        head.put(std::uint32_t(infoIndex), 4); head.put(std::uint32_t(rateIndex), 2); head.put(0, 1); head.put(0, 1);
        head.put(channels == 1 ? 3 : 1, 2); head.put(0, 2); head.put(0, 1); head.put(1, 1); head.put(0, 2);
        std::copy(head.bytes().begin(), head.bytes().end(), file.begin());
        std::size_t p = 4 + std::size_t(sideInfoBytes);
        const auto put32 = [&](std::uint32_t v) { for (int i = 3; i >= 0; --i) file[p++] = std::uint8_t(v >> (8 * i)); };
        std::memcpy(&file[p], "Info", 4); p += 4;
        put32(0x0F);                                   // frames, bytes, TOC, quality
        put32(std::uint32_t(frameCount));
        put32(std::uint32_t(file.size()));
        for (int i = 0; i < 100; ++i) file[p++] = std::uint8_t(i * 256 / 100);
        put32(0);
        const std::size_t lame = p;
        std::memcpy(&file[lame], "LAMEchmk ", 9);      // the LAME tag layout; "chmk": this app's encoder
        file[lame + 9] = 0x01;                         // tag revision 0, CBR
        file[lame + 10] = std::uint8_t(std::min(255.0, lowpassHz / 100.0));
        file[lame + 20] = std::uint8_t(std::min(255, options.bitrateKbps));
        file[lame + 21] = std::uint8_t(tagDelay >> 4);
        file[lame + 22] = std::uint8_t(((tagDelay & 0xF) << 4) | ((padding >> 8) & 0xF));
        file[lame + 23] = std::uint8_t(padding & 0xFF);
        const std::uint32_t music = std::uint32_t(file.size() - infoBytes);
        for (int i = 0; i < 4; ++i) file[lame + 28 + std::size_t(i)] = std::uint8_t(music >> (8 * (3 - i)));
        const std::uint16_t musicCrc = crc16(file.data() + infoBytes, file.size() - infoBytes);
        file[lame + 32] = std::uint8_t(musicCrc >> 8);
        file[lame + 33] = std::uint8_t(musicCrc & 0xFF);
        const std::uint16_t tagCrc = crc16(file.data(), lame + 34);
        file[lame + 34] = std::uint8_t(tagCrc >> 8);
        file[lame + 35] = std::uint8_t(tagCrc & 0xFF);
    }
    return file;
}

void writeMp3(const std::string& path, const std::vector<std::vector<float>>& channels, double sampleRate, const Mp3Options& options)
{
    const auto bytes = encodeMp3(channels, sampleRate, options);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("Cannot write " + path + ".");
    file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    file.flush();
    if (!file) throw std::runtime_error("Writing " + path + " failed; check the disk space.");
}

} // namespace audiofile
