#include "audiofile/md5.h"

#include <cstring>

namespace audiofile {

namespace {

constexpr std::uint32_t K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};
constexpr int S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

std::uint32_t rotl(std::uint32_t x, int n) noexcept { return (x << n) | (x >> (32 - n)); }

} // namespace

void Md5::block(const std::uint8_t* p) noexcept
{
    std::uint32_t m[16];
    for (int i = 0; i < 16; ++i)
        m[i] = std::uint32_t(p[4 * i]) | std::uint32_t(p[4 * i + 1]) << 8
             | std::uint32_t(p[4 * i + 2]) << 16 | std::uint32_t(p[4 * i + 3]) << 24;
    std::uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
    for (int i = 0; i < 64; ++i) {
        std::uint32_t f;
        int g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
        else { f = c ^ (b | ~d); g = (7 * i) % 16; }
        const std::uint32_t next = d;
        d = c;
        c = b;
        b = b + rotl(a + f + K[i] + m[g], S[i]);
        a = next;
    }
    m_state[0] += a; m_state[1] += b; m_state[2] += c; m_state[3] += d;
}

void Md5::add(std::span<const std::uint8_t> bytes) noexcept
{
    m_length += bytes.size();
    std::size_t i = 0;
    if (m_buffered > 0) {
        const std::size_t take = std::min(bytes.size(), 64 - m_buffered);
        std::memcpy(m_buffer.data() + m_buffered, bytes.data(), take);
        m_buffered += take;
        i = take;
        if (m_buffered < 64) return;
        block(m_buffer.data());
        m_buffered = 0;
    }
    for (; i + 64 <= bytes.size(); i += 64) block(bytes.data() + i);
    m_buffered = bytes.size() - i;
    if (m_buffered > 0) std::memcpy(m_buffer.data(), bytes.data() + i, m_buffered);
}

Md5::Digest Md5::finish() noexcept
{
    const std::uint64_t bits = m_length * 8;
    const std::uint8_t one = 0x80;
    add({&one, 1});
    const std::uint8_t zero = 0;
    while (m_buffered != 56) add({&zero, 1});
    std::uint8_t length[8];
    for (int i = 0; i < 8; ++i) length[i] = std::uint8_t(bits >> (8 * i));
    add({length, 8});
    Digest out{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out[std::size_t(4 * i + j)] = std::uint8_t(m_state[std::size_t(i)] >> (8 * j));
    return out;
}

} // namespace audiofile
