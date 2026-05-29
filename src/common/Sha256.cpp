#include "edge_tts/common/Sha256.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

// SHA-256 implementation following FIPS 180-4.
// Test vectors confirmed against Python hashlib and the system sha256sum utility.

namespace edge_tts::common {
namespace {

constexpr std::uint32_t rotr32(std::uint32_t x, unsigned n) noexcept {
    return (x >> n) | (x << (32u - n));
}

// Round constants: first 32 bits of the fractional parts of
// the cube roots of the first 64 primes.
constexpr std::array<std::uint32_t, 64> K = {{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
}};

// Initial hash values: first 32 bits of the fractional parts of
// the square roots of the first 8 primes.
constexpr std::array<std::uint32_t, 8> H0 = {{
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
}};

void sha256_block(std::array<std::uint32_t, 8>& h,
                  const std::uint8_t* block) noexcept
{
    std::array<std::uint32_t, 64> w{};

    // Prepare message schedule
    for (unsigned i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24u)
             | (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16u)
             | (static_cast<std::uint32_t>(block[i * 4 + 2]) <<  8u)
             | (static_cast<std::uint32_t>(block[i * 4 + 3]));
    }
    for (unsigned i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr32(w[i-15], 7) ^ rotr32(w[i-15], 18) ^ (w[i-15] >>  3u);
        const std::uint32_t s1 = rotr32(w[i- 2],17) ^ rotr32(w[i- 2], 19) ^ (w[i- 2] >> 10u);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    // Working variables
    auto [a, b, c, d, e, f, g, hh] = h;

    // 64 rounds
    for (unsigned i = 0; i < 64; ++i) {
        const std::uint32_t S1  = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        const std::uint32_t ch  = (e & f) ^ (~e & g);
        const std::uint32_t tmp1 = hh + S1 + ch + K[i] + w[i];
        const std::uint32_t S0  = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t tmp2 = S0 + maj;

        hh = g; g = f; f = e; e = d + tmp1;
        d  = c; c = b; b = a; a = tmp1 + tmp2;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

// Produces 32 raw bytes of digest.
std::array<std::uint8_t, 32> sha256_raw(std::string_view data) noexcept
{
    auto h = H0;

    const auto len = static_cast<std::uint64_t>(data.size());
    const std::uint64_t bit_len = len * 8u;

    // Process all full 64-byte blocks.
    std::uint64_t processed = 0;
    while (processed + 64 <= len) {
        sha256_block(h, reinterpret_cast<const std::uint8_t*>(data.data()) + processed);
        processed += 64;
    }

    // Padding: one '1' bit, zeros, then 64-bit big-endian message length.
    std::uint8_t pad[128]{};
    const std::uint64_t remaining = len - processed;
    std::memcpy(pad, data.data() + processed, static_cast<std::size_t>(remaining));
    pad[remaining] = 0x80u;

    // If there is not enough room for the 8-byte length, use two blocks.
    const unsigned pad_blocks = (remaining < 56) ? 1u : 2u;

    // Write bit_len as big-endian 64-bit in the last 8 bytes of the final block.
    std::uint8_t* len_field = pad + (pad_blocks * 64u) - 8u;
    for (unsigned i = 0; i < 8; ++i) {
        len_field[7 - i] = static_cast<std::uint8_t>(bit_len >> (8u * i));
    }

    for (unsigned b = 0; b < pad_blocks; ++b) {
        sha256_block(h, pad + b * 64u);
    }

    // Produce big-endian digest bytes.
    std::array<std::uint8_t, 32> digest{};
    for (unsigned i = 0; i < 8; ++i) {
        digest[i * 4 + 0] = static_cast<std::uint8_t>(h[i] >> 24u);
        digest[i * 4 + 1] = static_cast<std::uint8_t>(h[i] >> 16u);
        digest[i * 4 + 2] = static_cast<std::uint8_t>(h[i] >>  8u);
        digest[i * 4 + 3] = static_cast<std::uint8_t>(h[i]);
    }
    return digest;
}

constexpr char k_hex_lower[] = "0123456789abcdef";
constexpr char k_hex_upper[] = "0123456789ABCDEF";

std::string to_hex(const std::array<std::uint8_t, 32>& digest,
                   const char* alphabet) noexcept
{
    std::string out(64, '\0');
    for (unsigned i = 0; i < 32; ++i) {
        out[i * 2 + 0] = alphabet[digest[i] >> 4u];
        out[i * 2 + 1] = alphabet[digest[i] & 0xFu];
    }
    return out;
}

} // namespace

std::string sha256_hex(std::string_view data)
{
    return to_hex(sha256_raw(data), k_hex_lower);
}

std::string sha256_hex_upper(std::string_view data)
{
    return to_hex(sha256_raw(data), k_hex_upper);
}

} // namespace edge_tts::common
