#pragma once

#include <array>
#include <cstdint>
#include <span>

// MD5 (RFC 1321), fed in pieces. FLAC files carry the MD5 of their decoded samples; the FLAC
// reader checks it with this. Not for anything secret: MD5 only spots damage.
namespace audiofile {

class Md5 {
public:
    using Digest = std::array<std::uint8_t, 16>;

    void add(std::span<const std::uint8_t> bytes) noexcept;
    Digest finish() noexcept;                       // the object is spent afterwards

private:
    void block(const std::uint8_t* data) noexcept;

    std::array<std::uint32_t, 4> m_state{0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    std::array<std::uint8_t, 64> m_buffer{};
    std::size_t m_buffered = 0;
    std::uint64_t m_length = 0;                     // bytes added
};

} // namespace audiofile
