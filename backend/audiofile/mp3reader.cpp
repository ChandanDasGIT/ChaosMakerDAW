#include "audiofile/mp3reader.h"
#include "audiofile/mp3tables.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace audiofile {
namespace {

constexpr double pi = 3.14159265358979323846;

[[noreturn]] void fail(const std::string& path, const std::string& why)
{
    throw std::runtime_error(std::filesystem::path(path).filename().string() + ": " + why);
}

// ---- frame header ---------------------------------------------------------------------------

struct Header {
    int version = 1;          // 1, 2 or 25
    bool lsf = false;         // MPEG-2 / 2.5: one granule, the LSF scale factor coding
    int bitrateKbps = 0;
    int sampleRate = 0;
    int bandTable = 0;        // row of the scale factor band tables
    bool crc = false;
    int mode = 0;             // 0 stereo, 1 joint stereo, 2 dual channel, 3 mono
    int modeExtension = 0;
    int frameBytes = 0;
    int channels = 2;
    int sideInfoBytes = 0;
    int granules = 2;
};

constexpr std::array<int, 15> bitratesV1{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
constexpr std::array<int, 15> bitratesV2{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
constexpr std::array<int, 3> ratesV1{44100, 48000, 32000};

// Reads a Layer III header at p; false for anything else (sync noise, Layer I / II, free
// format, reserved values).
bool parseHeader(const std::uint8_t* p, std::size_t available, Header& h)
{
    if (available < 4 || p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) return false;
    const int versionBits = (p[1] >> 3) & 3;
    const int layer = (p[1] >> 1) & 3;
    const int bitrateIndex = p[2] >> 4;
    const int rateIndex = (p[2] >> 2) & 3;
    if (versionBits == 1 || layer != 1 || bitrateIndex == 0 || bitrateIndex == 15 || rateIndex == 3) return false;
    h.version = versionBits == 3 ? 1 : versionBits == 2 ? 2 : 25;
    h.lsf = h.version != 1;
    h.bitrateKbps = h.lsf ? bitratesV2[std::size_t(bitrateIndex)] : bitratesV1[std::size_t(bitrateIndex)];
    const int divisor = h.version == 1 ? 1 : h.version == 2 ? 2 : 4;
    h.sampleRate = ratesV1[std::size_t(rateIndex)] / divisor;
    h.bandTable = (h.version == 1 ? 0 : h.version == 2 ? 3 : 6) + rateIndex;
    h.crc = (p[1] & 1) == 0;
    const bool padding = (p[2] >> 1) & 1;
    h.mode = p[3] >> 6;
    h.modeExtension = (p[3] >> 4) & 3;
    h.channels = h.mode == 3 ? 1 : 2;
    h.granules = h.lsf ? 1 : 2;
    h.sideInfoBytes = h.lsf ? (h.channels == 1 ? 9 : 17) : (h.channels == 1 ? 17 : 32);
    h.frameBytes = (h.lsf ? 72000 : 144000) * h.bitrateKbps / h.sampleRate + (padding ? 1 : 0);
    return h.frameBytes > 4 + h.sideInfoBytes + (h.crc ? 2 : 0);
}

// ---- bits -----------------------------------------------------------------------------------

class Bits {
public:
    Bits(const std::uint8_t* data, std::size_t bytes) : m_data(data), m_bits(bytes * 8) {}
    std::uint32_t read(int n)
    {
        std::uint32_t v = 0;
        for (int i = 0; i < n; ++i) v = (v << 1) | bit();
        return v;
    }
    std::uint32_t bit()
    {
        if (m_pos >= m_bits) { ++m_pos; return 0; }   // past the end reads zeros
        const std::uint32_t b = (m_data[m_pos >> 3] >> (7 - (m_pos & 7))) & 1;
        ++m_pos;
        return b;
    }
    std::size_t position() const { return m_pos; }
    void seek(std::size_t pos) { m_pos = pos; }

private:
    const std::uint8_t* m_data;
    std::size_t m_bits;
    std::size_t m_pos = 0;
};

// ---- Huffman trees, built once from the code tables -----------------------------------------

struct Tree {
    // nodes[i] = {child for 0, child for 1}; a child >= 0 is a node, < 0 a leaf: -(value + 1).
    std::vector<std::array<int, 2>> nodes{{0, 0}};
    void insert(std::uint32_t code, int length, int value)
    {
        int node = 0;
        for (int i = length - 1; i >= 0; --i) {
            const int b = (code >> i) & 1;
            if (i == 0) { nodes[std::size_t(node)][std::size_t(b)] = -(value + 1); break; }
            int next = nodes[std::size_t(node)][std::size_t(b)];
            if (next <= 0) {
                next = int(nodes.size());
                nodes.push_back({0, 0});
                nodes[std::size_t(node)][std::size_t(b)] = next;
            }
            node = next;
        }
    }
    int decode(Bits& bits) const
    {
        int node = 0;
        for (int guard = 0; guard < 32; ++guard) {
            const int next = nodes[std::size_t(node)][bits.bit()];
            if (next < 0) return -next - 1;
            if (next == 0) return 0;   // a damaged stream: treat as the shortest value
            node = next;
        }
        return 0;
    }
};

struct Trees {
    std::array<Tree, 25> big;   // by code table number
    Tree quadA;
    Trees()
    {
        for (int n : {1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 24}) {
            const auto table = mp3tables::huffTable(n);
            for (int i = 0; i < table.count; ++i) {
                const auto& e = table.entries[i];
                big[std::size_t(n)].insert(e.code, e.length, (e.x << 4) | e.y);
            }
        }
        for (int v = 0; v < 16; ++v) quadA.insert(mp3tables::quadACodes[std::size_t(v)], mp3tables::quadALengths[std::size_t(v)], v);
    }
};

const Trees& trees()
{
    static const Trees instance;
    return instance;
}

// ---- side info ------------------------------------------------------------------------------

struct Granule {
    int part23 = 0, bigValues = 0, globalGain = 0, sfCompress = 0;
    bool windowSwitching = false, mixed = false;
    int blockType = 0;
    std::array<int, 3> table{};
    std::array<int, 3> subblockGain{};
    int region0 = 0, region1 = 0;
    bool preflag = false;
    int sfScale = 0, count1Table = 0;
};

struct SideInfo {
    int mainDataBegin = 0;
    std::array<std::array<int, 4>, 2> scfsi{};
    std::array<std::array<Granule, 2>, 2> gr{};   // [granule][channel]
};

SideInfo readSideInfo(const Header& h, const std::uint8_t* p)
{
    Bits b(p, std::size_t(h.sideInfoBytes));
    SideInfo s;
    s.mainDataBegin = int(b.read(h.lsf ? 8 : 9));
    b.read(h.lsf ? (h.channels == 1 ? 1 : 2) : (h.channels == 1 ? 5 : 3));   // private bits
    if (!h.lsf)
        for (int ch = 0; ch < h.channels; ++ch)
            for (int band = 0; band < 4; ++band) s.scfsi[std::size_t(ch)][std::size_t(band)] = int(b.read(1));
    for (int gr = 0; gr < h.granules; ++gr)
        for (int ch = 0; ch < h.channels; ++ch) {
            auto& g = s.gr[std::size_t(gr)][std::size_t(ch)];
            g.part23 = int(b.read(12));
            g.bigValues = std::min(288, int(b.read(9)));
            g.globalGain = int(b.read(8));
            g.sfCompress = int(b.read(h.lsf ? 9 : 4));
            g.windowSwitching = b.read(1);
            if (g.windowSwitching) {
                g.blockType = int(b.read(2));
                g.mixed = b.read(1);
                for (int r = 0; r < 2; ++r) g.table[std::size_t(r)] = int(b.read(5));
                for (int w = 0; w < 3; ++w) g.subblockGain[std::size_t(w)] = int(b.read(3));
                if (g.blockType == 0) g.blockType = 1;   // reserved; read as a start block
            } else {
                for (int r = 0; r < 3; ++r) g.table[std::size_t(r)] = int(b.read(5));
                g.region0 = int(b.read(4));
                g.region1 = int(b.read(3));
            }
            if (!h.lsf) g.preflag = b.read(1);
            g.sfScale = int(b.read(1));
            g.count1Table = int(b.read(1));
        }
    return s;
}

// ---- the decoder ----------------------------------------------------------------------------

struct Bands {
    std::array<int, 23> longStart{};    // line where each long band starts (22 bands, then 576)
    std::array<int, 14> shortStart{};   // per window (13 bands, then 192)
    explicit Bands(int table)
    {
        for (int i = 0; i < 22; ++i) longStart[std::size_t(i + 1)] = longStart[std::size_t(i)] + mp3tables::longBands[std::size_t(table)][std::size_t(i)];
        for (int i = 0; i < 13; ++i) shortStart[std::size_t(i + 1)] = shortStart[std::size_t(i)] + mp3tables::shortBands[std::size_t(table)][std::size_t(i)];
    }
};

struct ScaleFactors {
    std::array<int, 22> l{};
    std::array<std::array<int, 3>, 13> s{};
    // The largest value each band's slen allows (LSF intensity stereo: that value means "none").
    std::array<int, 22> lMax{};
    std::array<int, 13> sMax{};
};

struct Tables {
    std::vector<double> pow43;                   // |v|^(4/3), v up to 8206
    std::array<std::array<double, 18>, 36> imdctLong{};
    std::array<std::array<double, 6>, 12> imdctShort{};
    std::array<std::array<double, 36>, 4> windows{};   // block types 0..3 (2 unused here)
    std::array<double, 12> shortWindow{};
    std::array<double, 8> cs{}, ca{};
    std::array<std::array<double, 32>, 64> synthesis{};
    std::array<double, 512> d{};
    Tables()
    {
        pow43.resize(8207);
        for (std::size_t i = 0; i < pow43.size(); ++i) pow43[i] = std::pow(double(i), 4.0 / 3.0);
        for (int n = 0; n < 36; ++n)
            for (int k = 0; k < 18; ++k) imdctLong[std::size_t(n)][std::size_t(k)] = std::cos(pi / 72.0 * (2 * n + 1 + 18) * (2 * k + 1));
        for (int n = 0; n < 12; ++n)
            for (int k = 0; k < 6; ++k) imdctShort[std::size_t(n)][std::size_t(k)] = std::cos(pi / 24.0 * (2 * n + 1 + 6) * (2 * k + 1));
        for (int n = 0; n < 36; ++n) {
            const double sine = std::sin(pi / 36.0 * (n + 0.5));
            windows[0][std::size_t(n)] = sine;
            windows[1][std::size_t(n)] = n < 18 ? sine : n < 24 ? 1.0 : n < 30 ? std::sin(pi / 12.0 * (n - 18 + 0.5)) : 0.0;
            windows[3][std::size_t(n)] = n < 6 ? 0.0 : n < 12 ? std::sin(pi / 12.0 * (n - 6 + 0.5)) : n < 18 ? 1.0 : sine;
        }
        for (int n = 0; n < 12; ++n) shortWindow[std::size_t(n)] = std::sin(pi / 12.0 * (n + 0.5));
        const double c[8]{-0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037};
        for (int i = 0; i < 8; ++i) {
            const double root = std::sqrt(1.0 + c[i] * c[i]);
            cs[std::size_t(i)] = 1.0 / root;
            ca[std::size_t(i)] = c[i] / root;
        }
        for (int i = 0; i < 64; ++i)
            for (int k = 0; k < 32; ++k) synthesis[std::size_t(i)][std::size_t(k)] = std::cos((16 + i) * (2 * k + 1) * pi / 64.0);
        for (int i = 0; i < 512; ++i) d[std::size_t(i)] = mp3tables::synthesisWindow(i);
    }
};

const Tables& tables()
{
    static const Tables instance;
    return instance;
}

struct ChannelState {
    std::array<std::array<double, 18>, 32> overlap{};
    std::array<double, 1024> v{};
    ScaleFactors previous;   // granule 0's, for scfsi
};

class Decoder {
public:
    Decoder(const Header& first) : m_bands(first.bandTable) {}

    // One frame's granules; appends samples to out (one vector per channel).
    void decodeFrame(const Header& h, SideInfo& side, const std::uint8_t* mainData, std::size_t mainBytes,
                     std::vector<std::vector<float>>& out);
    void silentFrame(const Header& h, std::vector<std::vector<float>>& out)
    {
        for (auto& c : out) c.insert(c.end(), std::size_t(h.granules * 576), 0.0f);
    }

private:
    void readScaleFactors(const Header& h, SideInfo& side, int gr, int ch, Bits& bits, ScaleFactors& sf);
    int readHuffman(const Granule& g, Bits& bits, std::size_t end, std::array<int, 576>& values);
    void requantize(const Granule& g, const ScaleFactors& sf, const std::array<int, 576>& values, std::array<double, 576>& xr);
    void stereo(const Header& h, const Granule& right, const ScaleFactors& sfRight,
                std::array<double, 576>& left, std::array<double, 576>& rightXr);
    void synthesize(const Granule& g, std::array<double, 576>& xr, ChannelState& state, std::vector<float>& out);

    Bands m_bands;
    std::array<ChannelState, 2> m_channels;
};

void Decoder::readScaleFactors(const Header& h, SideInfo& side, int gr, int ch, Bits& bits, ScaleFactors& sf)
{
    auto& g = side.gr[std::size_t(gr)][std::size_t(ch)];
    sf = ScaleFactors{};
    const bool shortBlocks = g.windowSwitching && g.blockType == 2;
    if (!h.lsf) {
        const int slen1 = mp3tables::slen1[std::size_t(g.sfCompress)], slen2 = mp3tables::slen2[std::size_t(g.sfCompress)];
        if (shortBlocks) {
            int sfb = 0;
            if (g.mixed) for (; sfb < 8; ++sfb) sf.l[std::size_t(sfb)] = int(bits.read(slen1));
            for (int b = g.mixed ? 3 : 0; b < 12; ++b)
                for (int w = 0; w < 3; ++w) sf.s[std::size_t(b)][std::size_t(w)] = int(bits.read(b < 6 ? slen1 : slen2));
        } else {
            constexpr int groupStart[5]{0, 6, 11, 16, 21};
            auto& previous = m_channels[std::size_t(ch)].previous;
            for (int group = 0; group < 4; ++group) {
                const bool reuse = gr == 1 && side.scfsi[std::size_t(ch)][std::size_t(group)];
                for (int b = groupStart[group]; b < groupStart[group + 1]; ++b)
                    sf.l[std::size_t(b)] = reuse ? previous.l[std::size_t(b)] : int(bits.read(group < 2 ? slen1 : slen2));
            }
            if (gr == 0) previous = sf;
        }
        return;
    }
    // MPEG-2 / 2.5: four groups of bands with their own lengths.
    int slen[4]{};
    int table = 0;
    const bool intensityRight = ch == 1 && (h.mode == 1) && (h.modeExtension & 1);
    int sfc = g.sfCompress;
    bool preflag = false;
    if (intensityRight) {
        sfc >>= 1;
        if (sfc < 180) { slen[0] = sfc / 36; slen[1] = (sfc % 36) / 6; slen[2] = (sfc % 36) % 6; table = 3; }
        else if (sfc < 244) { sfc -= 180; slen[0] = (sfc % 64) >> 4; slen[1] = (sfc % 16) >> 2; slen[2] = sfc % 4; table = 4; }
        else { sfc -= 244; slen[0] = sfc / 3; slen[1] = sfc % 3; table = 5; }
    } else {
        if (sfc < 400) { slen[0] = (sfc >> 4) / 5; slen[1] = (sfc >> 4) % 5; slen[2] = (sfc & 15) >> 2; slen[3] = sfc & 3; table = 0; }
        else if (sfc < 500) { sfc -= 400; slen[0] = (sfc >> 2) / 5; slen[1] = (sfc >> 2) % 5; slen[2] = sfc & 3; table = 1; }
        else { sfc -= 500; slen[0] = sfc / 3; slen[1] = sfc % 3; preflag = true; table = 2; }
    }
    g.preflag = preflag;   // the LSF scale factor coding sets it
    const int kind = shortBlocks ? (g.mixed ? 2 : 1) : 0;
    int k = 0;
    for (int group = 0; group < 4; ++group) {
        const int count = mp3tables::lsfBandCounts[table][kind][group];
        for (int i = 0; i < count; ++i, ++k) {
            const int value = int(bits.read(slen[group]));
            const int maxValue = (1 << slen[group]) - 1;
            if (kind == 0) {
                if (k < 22) { sf.l[std::size_t(k)] = value; sf.lMax[std::size_t(k)] = maxValue; }
            } else if (kind == 2 && k < 6) {
                sf.l[std::size_t(k)] = value; sf.lMax[std::size_t(k)] = maxValue;
            } else {
                const int index = kind == 2 ? k - 6 : k;
                const int band = (kind == 2 ? 3 : 0) + index / 3;
                if (band < 13) { sf.s[std::size_t(band)][std::size_t(index % 3)] = value; sf.sMax[std::size_t(band)] = maxValue; }
            }
        }
    }
}

int Decoder::readHuffman(const Granule& g, Bits& bits, std::size_t end, std::array<int, 576>& values)
{
    values.fill(0);
    const auto& t = trees();
    int region1Start, region2Start;
    if (g.windowSwitching) {
        // The standard's fixed split for switched blocks: 36 lines for short blocks, the start of
        // long band 8 otherwise (36, or 54 / 108 at the LSF rates); the rest is region 1.
        region1Start = g.blockType == 2 ? 36 : m_bands.longStart[8];
        region2Start = 576;
    } else {
        region1Start = m_bands.longStart[std::size_t(std::min(22, g.region0 + 1))];
        region2Start = m_bands.longStart[std::size_t(std::min(22, g.region0 + g.region1 + 2))];
    }
    const int bigEnd = g.bigValues * 2;
    int i = 0;
    for (; i < bigEnd; i += 2) {
        const int region = i < region1Start ? 0 : i < region2Start ? 1 : 2;
        const auto info = mp3tables::bigValueTables[std::size_t(g.table[std::size_t(region)])];
        if (info.codes == 0) { values[std::size_t(i)] = values[std::size_t(i + 1)] = 0; continue; }
        const int pair = t.big[std::size_t(info.codes)].decode(bits);
        int x = pair >> 4, y = pair & 15;
        if (info.linbits && x == 15) x += int(bits.read(info.linbits));
        if (x && bits.bit()) x = -x;
        if (info.linbits && y == 15) y += int(bits.read(info.linbits));
        if (y && bits.bit()) y = -y;
        values[std::size_t(i)] = x;
        values[std::size_t(i + 1)] = y;
    }
    // count1: quadruples of -1, 0, 1 until the granule's bits are used.
    while (i + 4 <= 576 && bits.position() < end) {
        const std::size_t start = bits.position();
        const int quad = g.count1Table ? 15 - int(bits.read(4)) : t.quadA.decode(bits);
        int q[4]{(quad >> 3) & 1, (quad >> 2) & 1, (quad >> 1) & 1, quad & 1};
        for (int& v : q) if (v && bits.bit()) v = -v;
        if (bits.position() > end) { bits.seek(start); break; }   // an over-read quad is not part of it
        for (int n = 0; n < 4; ++n) values[std::size_t(i + n)] = q[n];
        i += 4;
    }
    return i;   // lines past this are zero
}

void Decoder::requantize(const Granule& g, const ScaleFactors& sf, const std::array<int, 576>& values, std::array<double, 576>& xr)
{
    const auto& tb = tables();
    const double multiplier = g.sfScale ? 1.0 : 0.5;
    const double base = 0.25 * (g.globalGain - 210);
    const auto value = [&](int v, double exponent) {
        const double magnitude = tb.pow43[std::size_t(std::min(std::abs(v), 8206))] * std::exp2(exponent);
        return v < 0 ? -magnitude : magnitude;
    };
    const bool shortBlocks = g.windowSwitching && g.blockType == 2;
    int line = 0;
    if (!shortBlocks || g.mixed) {
        const int longEnd = shortBlocks ? 36 : 576;
        for (int b = 0; b < 22 && line < longEnd; ++b) {
            const int pre = g.preflag ? mp3tables::pretab[std::size_t(b)] : 0;
            const double exponent = base - multiplier * (sf.l[std::size_t(b)] + pre);
            for (; line < std::min(longEnd, m_bands.longStart[std::size_t(b + 1)]); ++line)
                xr[std::size_t(line)] = values[std::size_t(line)] ? value(values[std::size_t(line)], exponent) : 0.0;
        }
    }
    if (shortBlocks) {
        for (int b = g.mixed ? 3 : 0; b < 13; ++b) {
            const int width = m_bands.shortStart[std::size_t(b + 1)] - m_bands.shortStart[std::size_t(b)];
            for (int w = 0; w < 3; ++w) {
                const double exponent = 0.25 * (g.globalGain - 210 - 8 * g.subblockGain[std::size_t(w)])
                                      - multiplier * sf.s[std::size_t(b)][std::size_t(w)];
                for (int f = 0; f < width && line < 576; ++f, ++line)
                    xr[std::size_t(line)] = values[std::size_t(line)] ? value(values[std::size_t(line)], exponent) : 0.0;
            }
        }
    }
    for (; line < 576; ++line) xr[std::size_t(line)] = 0.0;
}

void Decoder::stereo(const Header& h, const Granule& g, const ScaleFactors& sfRight,
                     std::array<double, 576>& left, std::array<double, 576>& right)
{
    int lastRight = -1;   // the right channel's last non-zero line: intensity stereo starts above it
    for (int i = 575; i >= 0; --i) if (right[std::size_t(i)] != 0.0) { lastRight = i; break; }
    const bool ms = h.mode == 1 && (h.modeExtension & 2);
    const bool intensity = h.mode == 1 && (h.modeExtension & 1);
    std::array<bool, 576> isLine{};
    if (intensity) {
        const bool shortBlocks = g.windowSwitching && g.blockType == 2;
        const auto apply = [&](int line, int isPos, int maxPos, bool lsf, int sfCompress) {
            if (lsf) {
                if (isPos == maxPos) return;
                const double io = (sfCompress & 1) ? std::sqrt(0.5) : std::pow(0.5, 0.25);
                double kl = 1.0, kr = 1.0;
                if (isPos & 1) kl = std::pow(io, (isPos + 1) / 2); else if (isPos) kr = std::pow(io, isPos / 2);
                const double x = left[std::size_t(line)];
                left[std::size_t(line)] = x * kl;
                right[std::size_t(line)] = x * kr;
            } else {
                if (isPos >= 7) return;
                const double ratio = std::tan(isPos * pi / 12.0);
                const double x = left[std::size_t(line)];
                left[std::size_t(line)] = x * ratio / (1.0 + ratio);
                right[std::size_t(line)] = x / (1.0 + ratio);
            }
            isLine[std::size_t(line)] = true;
        };
        if (!shortBlocks) {
            int first = 0;
            if (lastRight >= 0) {
                while (first < 22 && m_bands.longStart[std::size_t(first + 1)] <= lastRight) ++first;
                ++first;   // the band after the one holding it
            }
            for (int b = first; b < 22; ++b) {
                const int sfb = std::min(b, 20);
                for (int line = m_bands.longStart[std::size_t(b)]; line < m_bands.longStart[std::size_t(b + 1)]; ++line)
                    apply(line, sfRight.l[std::size_t(sfb)], h.lsf ? sfRight.lMax[std::size_t(sfb)] : 7, h.lsf, g.sfCompress);
            }
        } else {
            // Per window: from the band after the right channel's last non-zero value.
            const int startBand = g.mixed ? 3 : 0;
            std::array<int, 3> lastBand{-1, -1, -1};
            int line = g.mixed ? 36 : 0;
            for (int b = startBand; b < 13; ++b) {
                const int width = m_bands.shortStart[std::size_t(b + 1)] - m_bands.shortStart[std::size_t(b)];
                for (int w = 0; w < 3; ++w)
                    for (int f = 0; f < width; ++f, ++line)
                        if (line < 576 && right[std::size_t(line)] != 0.0) lastBand[std::size_t(w)] = b;
            }
            line = g.mixed ? 36 : 0;
            for (int b = startBand; b < 13; ++b) {
                const int width = m_bands.shortStart[std::size_t(b + 1)] - m_bands.shortStart[std::size_t(b)];
                for (int w = 0; w < 3; ++w)
                    for (int f = 0; f < width; ++f, ++line)
                        if (line < 576 && b > lastBand[std::size_t(w)]) {
                            const int sfb = std::min(b, 11);
                            apply(line, sfRight.s[std::size_t(sfb)][std::size_t(w)], h.lsf ? sfRight.sMax[std::size_t(sfb)] : 7, h.lsf, g.sfCompress);
                        }
            }
        }
    }
    if (ms) {
        constexpr double invRoot2 = 0.70710678118654752440;
        for (std::size_t i = 0; i < 576; ++i) {
            if (isLine[i]) continue;
            const double m = left[i], s = right[i];
            left[i] = (m + s) * invRoot2;
            right[i] = (m - s) * invRoot2;
        }
    }
}

void Decoder::synthesize(const Granule& g, std::array<double, 576>& xr, ChannelState& state, std::vector<float>& out)
{
    const auto& tb = tables();
    const bool shortBlocks = g.windowSwitching && g.blockType == 2;
    // Short blocks: from (band, window, line) order to each window's 192 lines.
    std::array<std::array<double, 192>, 3> shortLines{};
    if (shortBlocks) {
        int line = g.mixed ? 36 : 0;
        for (int b = g.mixed ? 3 : 0; b < 13; ++b) {
            const int start = m_bands.shortStart[std::size_t(b)];
            const int width = m_bands.shortStart[std::size_t(b + 1)] - start;
            for (int w = 0; w < 3; ++w)
                for (int f = 0; f < width; ++f, ++line)
                    if (line < 576) shortLines[std::size_t(w)][std::size_t(start + f)] = xr[std::size_t(line)];
        }
    }
    // Alias reduction between long subbands (only the first boundary in a mixed block).
    const int aliasLimit = shortBlocks ? (g.mixed ? 1 : 0) : 31;
    for (int sb = 0; sb < aliasLimit; ++sb)
        for (int i = 0; i < 8; ++i) {
            const std::size_t a = std::size_t(sb * 18 + 17 - i), b = std::size_t((sb + 1) * 18 + i);
            const double lo = xr[a], hi = xr[b];
            xr[a] = lo * tb.cs[std::size_t(i)] - hi * tb.ca[std::size_t(i)];
            xr[b] = hi * tb.cs[std::size_t(i)] + lo * tb.ca[std::size_t(i)];
        }
    // IMDCT and overlap-add: 32 subbands x 18 time samples.
    std::array<std::array<double, 18>, 32> samples{};
    for (int sb = 0; sb < 32; ++sb) {
        std::array<double, 36> y{};
        const bool longHere = !shortBlocks || (g.mixed && sb < 2);
        if (longHere) {
            const int type = shortBlocks ? 0 : (g.windowSwitching ? g.blockType : 0);
            const auto& window = tb.windows[std::size_t(type == 2 ? 0 : type)];
            const double* X = &xr[std::size_t(sb * 18)];
            for (int n = 0; n < 36; ++n) {
                double sum = 0.0;
                for (int k = 0; k < 18; ++k) sum += X[k] * tb.imdctLong[std::size_t(n)][std::size_t(k)];
                y[std::size_t(n)] = sum * window[std::size_t(n)];
            }
        } else {
            for (int w = 0; w < 3; ++w) {
                const double* X = &shortLines[std::size_t(w)][std::size_t(sb * 6)];
                for (int n = 0; n < 12; ++n) {
                    double sum = 0.0;
                    for (int k = 0; k < 6; ++k) sum += X[k] * tb.imdctShort[std::size_t(n)][std::size_t(k)];
                    y[std::size_t(6 + 6 * w + n)] += sum * tb.shortWindow[std::size_t(n)];
                }
            }
        }
        for (int i = 0; i < 18; ++i) {
            samples[std::size_t(sb)][std::size_t(i)] = y[std::size_t(i)] + state.overlap[std::size_t(sb)][std::size_t(i)];
            state.overlap[std::size_t(sb)][std::size_t(i)] = y[std::size_t(i + 18)];
        }
        if (sb & 1)   // frequency inversion
            for (int i = 1; i < 18; i += 2) samples[std::size_t(sb)][std::size_t(i)] = -samples[std::size_t(sb)][std::size_t(i)];
    }
    // Polyphase synthesis: 18 slots of 32 subband samples -> 32 PCM samples each.
    for (int t = 0; t < 18; ++t) {
        std::memmove(&state.v[64], &state.v[0], sizeof(double) * (1024 - 64));
        for (int i = 0; i < 64; ++i) {
            double sum = 0.0;
            for (int k = 0; k < 32; ++k) sum += tb.synthesis[std::size_t(i)][std::size_t(k)] * samples[std::size_t(k)][std::size_t(t)];
            state.v[std::size_t(i)] = sum;
        }
        for (int j = 0; j < 32; ++j) {
            double sum = 0.0;
            for (int i = 0; i < 8; ++i) {
                sum += state.v[std::size_t(i * 128 + j)] * tb.d[std::size_t(i * 64 + j)];
                sum += state.v[std::size_t(i * 128 + 96 + j)] * tb.d[std::size_t(i * 64 + 32 + j)];
            }
            out.push_back(float(sum));
        }
    }
}

void Decoder::decodeFrame(const Header& h, SideInfo& side, const std::uint8_t* mainData, std::size_t mainBytes,
                          std::vector<std::vector<float>>& out)
{
    Bits bits(mainData, mainBytes);
    for (int gr = 0; gr < h.granules; ++gr) {
        std::array<std::array<double, 576>, 2> xr{};
        std::array<ScaleFactors, 2> sf;
        for (int ch = 0; ch < h.channels; ++ch) {
            const auto& g = side.gr[std::size_t(gr)][std::size_t(ch)];
            const std::size_t part2Start = bits.position();
            readScaleFactors(h, side, gr, ch, bits, sf[std::size_t(ch)]);
            std::array<int, 576> values{};
            readHuffman(g, bits, part2Start + std::size_t(g.part23), values);
            bits.seek(part2Start + std::size_t(g.part23));
            requantize(g, sf[std::size_t(ch)], values, xr[std::size_t(ch)]);
        }
        if (h.channels == 2 && h.mode == 1)
            stereo(h, side.gr[std::size_t(gr)][1], sf[1], xr[0], xr[1]);
        for (int ch = 0; ch < h.channels; ++ch)
            synthesize(side.gr[std::size_t(gr)][std::size_t(ch)], xr[std::size_t(ch)], m_channels[std::size_t(ch)], out[std::size_t(ch)]);
    }
}

std::size_t skipId3v2(std::span<const std::uint8_t> b)
{
    std::size_t pos = 0;
    while (b.size() >= pos + 10 && std::memcmp(b.data() + pos, "ID3", 3) == 0) {
        const std::size_t size = (std::size_t(b[pos + 6] & 0x7f) << 21) | (std::size_t(b[pos + 7] & 0x7f) << 14)
                               | (std::size_t(b[pos + 8] & 0x7f) << 7) | std::size_t(b[pos + 9] & 0x7f);
        pos += 10 + size + ((b[pos + 5] & 0x10) ? 10 : 0);
    }
    return pos;
}

// The first frame header whose next frame also has a matching header (or that reaches the end).
std::size_t findFirstFrame(std::span<const std::uint8_t> b, std::size_t from, Header& h)
{
    for (std::size_t p = from; p + 4 <= b.size(); ++p) {
        if (!parseHeader(b.data() + p, b.size() - p, h)) continue;
        const std::size_t next = p + std::size_t(h.frameBytes);
        Header n;
        if (next + 4 > b.size()) return p;
        if (parseHeader(b.data() + next, b.size() - next, n) && n.sampleRate == h.sampleRate && n.version == h.version) return p;
    }
    return b.size();
}

} // namespace

Mp3File decodeMp3(std::span<const std::uint8_t> bytes, const std::string& path)
{
    if (std::int64_t(bytes.size()) > MaxMp3Bytes) fail(path, "the file is larger than 1 GB.");
    Header first;
    std::size_t pos = findFirstFrame(bytes, skipId3v2(bytes), first);
    if (pos >= bytes.size()) fail(path, "no MPEG Layer III audio frames were found.");
    // The end: without a trailing ID3v1 tag.
    std::size_t end = bytes.size();
    if (end >= 128 && std::memcmp(bytes.data() + end - 128, "TAG", 3) == 0) end -= 128;

    Mp3File result;
    result.version = first.version;
    // A Xing / Info frame (no audio) may carry the LAME tag with the delay and padding.
    {
        const std::size_t sideStart = pos + 4 + (first.crc ? 2 : 0);
        const std::size_t tag = sideStart + std::size_t(first.sideInfoBytes);
        if (tag + 8 <= end && (std::memcmp(bytes.data() + tag, "Xing", 4) == 0 || std::memcmp(bytes.data() + tag, "Info", 4) == 0)) {
            const std::uint32_t flags = (std::uint32_t(bytes[tag + 4]) << 24) | (std::uint32_t(bytes[tag + 5]) << 16)
                                      | (std::uint32_t(bytes[tag + 6]) << 8) | bytes[tag + 7];
            std::size_t lame = tag + 8;
            if (flags & 1) lame += 4;
            if (flags & 2) lame += 4;
            if (flags & 4) lame += 100;
            if (flags & 8) lame += 4;
            if (lame + 24 <= end && lame + 24 <= pos + std::size_t(first.frameBytes)) {
                result.encoderDelay = (int(bytes[lame + 21]) << 4) | (bytes[lame + 22] >> 4);
                result.encoderPadding = ((int(bytes[lame + 22]) & 0x0F) << 8) | bytes[lame + 23];
            }
            pos += std::size_t(first.frameBytes);   // not audio
        } else if (tag + 4 <= end && std::memcmp(bytes.data() + pos + 4 + 32, "VBRI", 4) == 0) {
            pos += std::size_t(first.frameBytes);
        }
    }

    Decoder decoder(first);
    std::vector<std::vector<float>> out(std::size_t(first.channels));
    std::vector<std::uint8_t> reservoir;
    std::int64_t frames = 0, bitrateSum = 0;
    int firstBitrate = -1;
    while (pos + 4 <= end) {
        Header h;
        if (!parseHeader(bytes.data() + pos, end - pos, h) || h.sampleRate != first.sampleRate || h.channels != first.channels) {
            // Lost sync (damage, a tag in the middle): look for the next good frame.
            const std::size_t next = findFirstFrame(bytes.subspan(0, end), pos + 1, h);
            if (next >= end) break;
            pos = next;
            continue;
        }
        if (pos + std::size_t(h.frameBytes) > end) break;   // a cut-off last frame
        const std::uint8_t* frame = bytes.data() + pos;
        const std::size_t sideStart = 4 + (h.crc ? 2 : 0);
        SideInfo side = readSideInfo(h, frame + sideStart);
        const std::size_t mainStart = sideStart + std::size_t(h.sideInfoBytes);
        const std::uint8_t* main = frame + mainStart;
        const std::size_t mainBytes = std::size_t(h.frameBytes) - mainStart;
        if (std::size_t(side.mainDataBegin) > reservoir.size()) {
            decoder.silentFrame(h, out);   // its start was in frames before the file starts
        } else {
            std::vector<std::uint8_t> data(reservoir.end() - side.mainDataBegin, reservoir.end());
            data.insert(data.end(), main, main + mainBytes);
            decoder.decodeFrame(h, side, data.data(), data.size(), out);
        }
        reservoir.insert(reservoir.end(), main, main + mainBytes);
        if (reservoir.size() > 4096) reservoir.erase(reservoir.begin(), reservoir.end() - 2048);
        ++frames;
        bitrateSum += h.bitrateKbps;
        if (firstBitrate < 0) firstBitrate = h.bitrateKbps;
        else if (h.bitrateKbps != firstBitrate) result.variableBitrate = true;
        pos += std::size_t(h.frameBytes);
    }
    if (frames == 0 || out[0].empty()) fail(path, "no MPEG Layer III audio frames could be decoded.");
    result.bitrateKbps = int(bitrateSum / frames);

    // Gapless: skip the encoder delay plus the decoder's 529 samples, drop the padding.
    std::size_t total = out[0].size();
    std::size_t skip = 0, keep = total;
    if (result.encoderDelay >= 0) {
        skip = std::size_t(result.encoderDelay) + 529;
        const std::int64_t length = std::int64_t(total) - result.encoderDelay - result.encoderPadding;
        keep = length > 0 ? std::size_t(length) : 0;
        if (skip >= total) { skip = 0; keep = total; }
        keep = std::min(keep, total - skip);
    }
    auto audio = std::make_shared<AudioData>();
    audio->path = path;
    audio->sampleRate = first.sampleRate;
    audio->frames = std::int64_t(keep);
    for (auto& c : out) audio->channels.emplace_back(c.begin() + std::ptrdiff_t(skip), c.begin() + std::ptrdiff_t(skip + keep));
    result.audio = std::move(audio);
    return result;
}

Mp3File readMp3(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) fail(path, "cannot open the file.");
    const auto size = std::int64_t(file.tellg());
    if (size > MaxMp3Bytes) fail(path, "the file is larger than 1 GB.");
    std::vector<std::uint8_t> bytes(std::size_t(std::max<std::int64_t>(0, size)));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!file) fail(path, "the file could not be read.");
    return decodeMp3(bytes, path);
}

} // namespace audiofile
