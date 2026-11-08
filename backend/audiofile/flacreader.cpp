#include "audiofile/flacreader.h"
#include "audiofile/md5.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace audiofile {

namespace {

[[noreturn]] void fail(const std::string& path, const std::string& why)
{
    throw std::runtime_error(path + ": " + why);
}

// Reads bits from the most significant end of each byte, as FLAC stores them.
class BitReader {
public:
    BitReader(std::span<const std::uint8_t> bytes, std::size_t at, const std::string& path)
        : m_bytes(bytes), m_bit(at * 8), m_path(path) {}

    std::size_t bytePosition() const noexcept { return m_bit / 8; }
    bool atByte() const noexcept { return m_bit % 8 == 0; }
    void alignToByte() noexcept { m_bit = (m_bit + 7) & ~std::size_t(7); }
    std::size_t bitsLeft() const noexcept { return m_bytes.size() * 8 - std::min(m_bit, m_bytes.size() * 8); }

    // Up to 64 bits, unsigned.
    std::uint64_t bits(int count)
    {
        if (count == 0) return 0;
        if (std::size_t(count) > bitsLeft()) fail(m_path, "the file ends in the middle of a frame");
        std::uint64_t value = 0;
        while (count > 0) {
            const std::size_t byte = m_bit / 8;
            const int offset = int(m_bit % 8);
            const int take = std::min(count, 8 - offset);
            const unsigned chunk = (unsigned(m_bytes[byte]) >> (8 - offset - take)) & ((1u << take) - 1);
            value = (value << take) | chunk;
            m_bit += std::size_t(take);
            count -= take;
        }
        return value;
    }

    std::int64_t signedBits(int count)
    {
        if (count == 0) return 0;
        const std::uint64_t raw = bits(count);
        const std::uint64_t sign = std::uint64_t(1) << (count - 1);
        return std::int64_t(raw ^ sign) - std::int64_t(sign);
    }

    // Zeros before the next 1 (the 1 is read too).
    std::uint32_t unary()
    {
        std::uint32_t zeros = 0;
        for (;;) {
            if (bitsLeft() == 0) fail(m_path, "the file ends in the middle of a frame");
            const std::size_t byte = m_bit / 8;
            const int offset = int(m_bit % 8);
            const unsigned rest = (unsigned(m_bytes[byte]) << offset) & 0xFFu;
            if (rest == 0) {                               // the rest of this byte is zeros
                zeros += std::uint32_t(8 - offset);
                m_bit += std::size_t(8 - offset);
                continue;
            }
            int lead = 0;
            while (!(rest & (0x80u >> lead))) ++lead;
            zeros += std::uint32_t(lead);
            m_bit += std::size_t(lead + 1);
            return zeros;
        }
    }

private:
    std::span<const std::uint8_t> m_bytes;
    std::size_t m_bit;
    const std::string& m_path;
};

std::uint8_t crc8(std::span<const std::uint8_t> bytes) noexcept
{
    std::uint8_t crc = 0;
    for (const std::uint8_t b : bytes) {
        crc ^= b;
        for (int i = 0; i < 8; ++i) crc = std::uint8_t((crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1);
    }
    return crc;
}

std::uint16_t crc16(std::span<const std::uint8_t> bytes) noexcept
{
    std::uint16_t crc = 0;
    for (const std::uint8_t b : bytes) {
        crc ^= std::uint16_t(b) << 8;
        for (int i = 0; i < 8; ++i) crc = std::uint16_t((crc & 0x8000) ? (crc << 1) ^ 0x8005 : crc << 1);
    }
    return crc;
}

struct StreamInfo {
    int minBlock = 0, maxBlock = 0;
    int sampleRate = 0, channels = 0, bitsPerSample = 0;
    std::uint64_t totalSamples = 0;                         // 0: not known
    std::array<std::uint8_t, 16> md5{};
};

// Residual of one subframe into out[order..blockSize).
void residual(BitReader& in, int blockSize, int order, std::int64_t* out, const std::string& path)
{
    const int method = int(in.bits(2));
    if (method > 1) fail(path, "unknown residual coding");
    const int paramBits = method == 0 ? 4 : 5;
    const unsigned escape = method == 0 ? 15u : 31u;
    const int partitionOrder = int(in.bits(4));
    const int partitions = 1 << partitionOrder;
    if ((blockSize >> partitionOrder) < order || blockSize % partitions != 0)
        fail(path, "a residual partition does not fit its block");
    int n = order;
    for (int p = 0; p < partitions; ++p) {
        const int count = (blockSize >> partitionOrder) - (p == 0 ? order : 0);
        const unsigned param = unsigned(in.bits(paramBits));
        if (param == escape) {
            const int raw = int(in.bits(5));
            for (int i = 0; i < count; ++i) out[n++] = in.signedBits(raw);
        } else {
            for (int i = 0; i < count; ++i) {
                const std::uint64_t q = in.unary();
                const std::uint64_t u = (q << param) | in.bits(int(param));
                out[n++] = std::int64_t(u >> 1) ^ -std::int64_t(u & 1);
            }
        }
    }
}

// One subframe of `bps` bits into out[0..blockSize).
void subframe(BitReader& in, int blockSize, int bps, std::int64_t* out, const std::string& path)
{
    if (in.bits(1) != 0) fail(path, "a subframe header is damaged");
    const int type = int(in.bits(6));
    int wasted = 0;
    if (in.bits(1)) wasted = int(in.unary()) + 1;
    if (wasted >= bps) fail(path, "a subframe wastes all its bits");
    const int width = bps - wasted;

    if (type == 0) {                                        // constant
        const std::int64_t v = in.signedBits(width);
        std::fill(out, out + blockSize, v);
    } else if (type == 1) {                                 // verbatim
        for (int i = 0; i < blockSize; ++i) out[i] = in.signedBits(width);
    } else if (type >= 8 && type <= 12) {                   // fixed prediction, order 0..4
        const int order = type - 8;
        if (order > blockSize) fail(path, "a prediction is longer than its block");
        for (int i = 0; i < order; ++i) out[i] = in.signedBits(width);
        residual(in, blockSize, order, out, path);
        for (int i = order; i < blockSize; ++i) {
            switch (order) {
            case 1: out[i] += out[i - 1]; break;
            case 2: out[i] += 2 * out[i - 1] - out[i - 2]; break;
            case 3: out[i] += 3 * out[i - 1] - 3 * out[i - 2] + out[i - 3]; break;
            case 4: out[i] += 4 * out[i - 1] - 6 * out[i - 2] + 4 * out[i - 3] - out[i - 4]; break;
            default: break;
            }
        }
    } else if (type >= 32) {                                // LPC, order 1..32
        const int order = type - 31;
        if (order > blockSize) fail(path, "a prediction is longer than its block");
        for (int i = 0; i < order; ++i) out[i] = in.signedBits(width);
        const int precision = int(in.bits(4)) + 1;
        if (precision == 16) fail(path, "a prediction has an invalid precision");
        const int shift = int(in.signedBits(5));
        if (shift < 0) fail(path, "a prediction has a negative shift");
        std::array<std::int64_t, 32> coef{};
        for (int i = 0; i < order; ++i) coef[std::size_t(i)] = in.signedBits(precision);
        residual(in, blockSize, order, out, path);
        for (int i = order; i < blockSize; ++i) {
            std::int64_t sum = 0;
            for (int j = 0; j < order; ++j) sum += coef[std::size_t(j)] * out[i - 1 - j];
            out[i] += sum >> shift;
        }
    } else {
        fail(path, "a subframe uses a reserved type");
    }
    if (wasted > 0)
        for (int i = 0; i < blockSize; ++i) out[i] = std::int64_t(std::uint64_t(out[i]) << wasted);
}

int blockSizeFor(int code, BitReader& in, const std::string& path)
{
    if (code == 1) return 192;
    if (code >= 2 && code <= 5) return 576 << (code - 2);
    if (code == 6) return int(in.bits(8)) + 1;
    if (code == 7) return int(in.bits(16)) + 1;
    if (code >= 8) return 256 << (code - 8);
    fail(path, "a frame uses a reserved block size");
}

} // namespace

FlacFile decodeFlac(std::span<const std::uint8_t> bytes, const std::string& path)
{
    std::size_t at = 0;
    // A leading ID3v2 tag: "ID3", version, flags, a 28-bit size in 7-bit bytes.
    if (bytes.size() >= 10 && bytes[0] == 'I' && bytes[1] == 'D' && bytes[2] == '3') {
        const std::size_t size = (std::size_t(bytes[6] & 0x7F) << 21) | (std::size_t(bytes[7] & 0x7F) << 14)
                               | (std::size_t(bytes[8] & 0x7F) << 7) | std::size_t(bytes[9] & 0x7F);
        at = 10 + size + ((bytes[5] & 0x10) ? 10 : 0);
    }
    if (bytes.size() < at + 4 || bytes[at] != 'f' || bytes[at + 1] != 'L' || bytes[at + 2] != 'a' || bytes[at + 3] != 'C')
        fail(path, "not a FLAC file");
    at += 4;

    // Metadata blocks: STREAMINFO comes first, the rest is skipped.
    StreamInfo info;
    bool haveInfo = false;
    for (bool last = false; !last;) {
        if (bytes.size() < at + 4) fail(path, "the file ends inside its header");
        last = bytes[at] & 0x80;
        const int type = bytes[at] & 0x7F;
        const std::size_t length = (std::size_t(bytes[at + 1]) << 16) | (std::size_t(bytes[at + 2]) << 8) | bytes[at + 3];
        at += 4;
        if (bytes.size() < at + length) fail(path, "the file ends inside its header");
        if (type == 0) {
            if (length < 34) fail(path, "the stream information is too short");
            BitReader in(bytes, at, path);
            info.minBlock = int(in.bits(16));
            info.maxBlock = int(in.bits(16));
            in.bits(24);
            in.bits(24);
            info.sampleRate = int(in.bits(20));
            info.channels = int(in.bits(3)) + 1;
            info.bitsPerSample = int(in.bits(5)) + 1;
            info.totalSamples = in.bits(36);
            for (auto& b : info.md5) b = std::uint8_t(in.bits(8));
            haveInfo = true;
        } else if (type == 127) {
            fail(path, "the header is damaged");
        }
        at += length;
    }
    if (!haveInfo) fail(path, "no stream information");
    if (info.sampleRate <= 0) fail(path, "no sample rate");
    if (info.bitsPerSample < 4) fail(path, "fewer than 4 bits per sample");

    const int channels = info.channels;
    std::vector<std::vector<std::int32_t>> pcm{std::size_t(channels)};
    if (info.totalSamples > 0 && info.totalSamples < std::uint64_t(MaxFlacBytes))
        for (auto& c : pcm) c.reserve(std::size_t(info.totalSamples));
    std::vector<std::int64_t> work(std::size_t(std::max(info.maxBlock, 1)) * std::size_t(channels));
    const bool md5Present = std::any_of(info.md5.begin(), info.md5.end(), [](std::uint8_t b) { return b != 0; });
    Md5 md5;
    const int md5Bytes = (info.bitsPerSample + 7) / 8;
    std::vector<std::uint8_t> md5Buffer;

    while (at + 2 <= bytes.size()) {
        // Find the next frame: sync code 0xFFF8 or 0xFFF9 (some encoders pad between frames).
        if (!(bytes[at] == 0xFF && (bytes[at + 1] & 0xFE) == 0xF8)) {
            if (std::all_of(bytes.begin() + std::ptrdiff_t(at), bytes.end(), [](std::uint8_t b) { return b == 0; }))
                break;                                      // trailing padding
            fail(path, "a frame does not start where the last one ended");
        }
        const std::size_t frameStart = at;
        BitReader in(bytes, at, path);
        in.bits(15);
        in.bits(1);                                         // blocking strategy: fixed or variable
        const int blockCode = int(in.bits(4));
        const int rateCode = int(in.bits(4));
        const int channelCode = int(in.bits(4));
        const int sizeCode = int(in.bits(3));
        if (in.bits(1) != 0) fail(path, "a frame header is damaged");
        // The frame or sample number, UTF-8 style: 1 to 7 bytes.
        const unsigned first = unsigned(in.bits(8));
        int extra = 0;
        if (first & 0x80) {
            while (extra < 7 && (first & (0x40u >> extra))) ++extra;
            if (extra == 0 || extra > 6) fail(path, "a frame number is damaged");
            for (int i = 0; i < extra; ++i)
                if ((in.bits(8) & 0xC0) != 0x80) fail(path, "a frame number is damaged");
        }
        const int blockSize = blockSizeFor(blockCode, in, path);
        if (rateCode == 12) in.bits(8);
        else if (rateCode == 13 || rateCode == 14) in.bits(16);
        else if (rateCode == 15) fail(path, "a frame uses an invalid sample rate");
        static constexpr int SampleSizes[8] = {0, 8, 12, 0, 16, 20, 24, 32};
        if (sizeCode == 3) fail(path, "a frame uses a reserved sample size");
        const int bps = sizeCode == 0 ? info.bitsPerSample : SampleSizes[sizeCode];
        if (bps != info.bitsPerSample) fail(path, "a frame changes the bits per sample");
        const std::size_t headerEnd = in.bytePosition();
        const std::uint8_t headerCrc = std::uint8_t(in.bits(8));
        if (crc8(bytes.subspan(frameStart, headerEnd - frameStart)) != headerCrc) fail(path, "a frame header is damaged (CRC)");

        int frameChannels = channels;
        if (channelCode <= 7) frameChannels = channelCode + 1;
        else if (channelCode <= 10) frameChannels = 2;
        else fail(path, "a frame uses a reserved channel layout");
        if (frameChannels != channels) fail(path, "a frame changes the number of channels");
        if (blockSize > int(work.size()) / channels) work.resize(std::size_t(blockSize) * std::size_t(channels));

        for (int c = 0; c < channels; ++c) {
            // The side channel carries one more bit.
            const bool side = (channelCode == 8 && c == 1) || (channelCode == 9 && c == 0) || (channelCode == 10 && c == 1);
            subframe(in, blockSize, bps + (side ? 1 : 0), work.data() + std::size_t(c) * std::size_t(blockSize), path);
        }
        in.alignToByte();
        const std::size_t crcAt = in.bytePosition();
        const std::uint16_t frameCrc = std::uint16_t(in.bits(16));
        if (crc16(bytes.subspan(frameStart, crcAt - frameStart)) != frameCrc) fail(path, "a frame is damaged (CRC)");
        at = in.bytePosition();

        std::int64_t* a = work.data();
        std::int64_t* b = work.data() + blockSize;
        for (int i = 0; i < blockSize && channelCode >= 8; ++i) {
            if (channelCode == 8) b[i] = a[i] - b[i];                   // left, side
            else if (channelCode == 9) a[i] = a[i] + b[i];              // side, right
            else {                                                      // mid, side
                const std::int64_t mid = (a[i] * 2) | (b[i] & 1);
                const std::int64_t s = b[i];
                a[i] = (mid + s) >> 1;
                b[i] = (mid - s) >> 1;
            }
        }
        for (int c = 0; c < channels; ++c) {
            const std::int64_t* src = work.data() + std::size_t(c) * std::size_t(blockSize);
            auto& dst = pcm[std::size_t(c)];
            for (int i = 0; i < blockSize; ++i) dst.push_back(std::int32_t(src[i]));
        }
        if (md5Present) {
            md5Buffer.resize(std::size_t(blockSize) * std::size_t(channels) * std::size_t(md5Bytes));
            std::size_t k = 0;
            for (int i = 0; i < blockSize; ++i)
                for (int c = 0; c < channels; ++c) {
                    const std::uint32_t v = std::uint32_t(work[std::size_t(c) * std::size_t(blockSize) + std::size_t(i)]);
                    for (int byte = 0; byte < md5Bytes; ++byte) md5Buffer[k++] = std::uint8_t(v >> (8 * byte));
                }
            md5.add(md5Buffer);
        }
    }

    const std::int64_t frames = std::int64_t(pcm[0].size());
    if (info.totalSamples > 0 && std::uint64_t(frames) != info.totalSamples)
        fail(path, "the file holds " + std::to_string(frames) + " samples, its header says "
                       + std::to_string(info.totalSamples));
    if (md5Present && md5.finish() != info.md5) fail(path, "the decoded audio does not match the file's MD5");

    FlacFile out;
    out.bitsPerSample = info.bitsPerSample;
    out.md5Checked = md5Present;
    out.audio = std::make_shared<AudioData>();
    out.audio->sampleRate = info.sampleRate;
    out.audio->frames = frames;
    out.audio->path = path;
    const double scale = 1.0 / double(std::int64_t(1) << (info.bitsPerSample - 1));
    out.audio->channels.resize(std::size_t(channels));
    for (int c = 0; c < channels; ++c) {
        auto& dst = out.audio->channels[std::size_t(c)];
        const auto& src = pcm[std::size_t(c)];
        dst.resize(src.size());
        for (std::size_t i = 0; i < src.size(); ++i) dst[i] = float(double(src[i]) * scale);
    }
    return out;
}

FlacFile readFlac(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) fail(path, "cannot be opened");
    const std::streamoff size = file.tellg();
    if (size < 0) fail(path, "cannot be read");
    if (size > MaxFlacBytes) fail(path, "is larger than 2 GB");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size)) fail(path, "cannot be read");
    return decodeFlac(bytes, path);
}

} // namespace audiofile
