#include "common/IdGenerator.hpp"
#include "common/Hex.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace edge_tts::common {

IdGenerator::IdGenerator() {
    std::random_device rd;
    // Seed with multiple 32-bit words to fill the full 624-element state.
    std::array<std::uint32_t, std::mt19937_64::state_size> seed_data;
    std::generate(seed_data.begin(), seed_data.end(), std::ref(rd));
    std::seed_seq seed(seed_data.begin(), seed_data.end());
    rng_.seed(seed);
}

void IdGenerator::fill_random_bytes(std::uint8_t* buf, std::size_t n) {
    std::size_t i = 0;
    while (i < n) {
        const std::uint64_t word = rng_();
        const std::size_t chunk = std::min<std::size_t>(8, n - i);
        std::memcpy(buf + i, &word, chunk);
        i += chunk;
    }
}

std::string IdGenerator::uuid_v4() {
    std::uint8_t b[16];
    fill_random_bytes(b, 16);

    // Set version 4 (upper nibble of byte 6)
    b[6] = static_cast<std::uint8_t>((b[6] & 0x0fu) | 0x40u);
    // Set RFC 4122 variant (upper 2 bits of byte 8 = 10xx)
    b[8] = static_cast<std::uint8_t>((b[8] & 0x3fu) | 0x80u);

    // Format: 8-4-4-4-12
    constexpr char hex[] = "0123456789abcdef";
    std::string s;
    s.reserve(36);
    const auto push = [&](std::size_t i) {
        s += hex[b[i] >> 4];
        s += hex[b[i] & 0x0fu];
    };

    for (std::size_t i = 0;  i < 4;  ++i) { push(i); } s += '-';
    for (std::size_t i = 4;  i < 6;  ++i) { push(i); } s += '-';
    for (std::size_t i = 6;  i < 8;  ++i) { push(i); } s += '-';
    for (std::size_t i = 8;  i < 10; ++i) { push(i); } s += '-';
    for (std::size_t i = 10; i < 16; ++i) { push(i); }

    return s;
}

std::string IdGenerator::uuid_v4_without_hyphens() {
    // Re-use uuid_v4 and strip the 4 hyphens; the version/variant bits are set.
    std::string with = uuid_v4();
    std::string without;
    without.reserve(32);
    for (const char c : with)
        if (c != '-') without += c;
    return without;
}

std::string IdGenerator::random_32_hex() {
    std::uint8_t b[16];
    fill_random_bytes(b, 16);
    const auto span = std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(b), 16};
    return hex_encode_lower(span);
}

} // namespace edge_tts::common
