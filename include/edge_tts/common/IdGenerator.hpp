#pragma once

#include <cstdint>
#include <random>
#include <string>

namespace edge_tts::common {

// Generates random identifiers using a per-instance Mersenne Twister PRNG
// seeded from std::random_device at construction time.
//
// Edge protocol usage (see docs/PROTOCOL_NOTES.md):
//   uuid_v4_without_hyphens() → ConnectionId, X-RequestId (lowercase, 32 chars)
//   random_32_hex().uppercase → MUID cookie value (32 uppercase hex chars)
//
// Edge-specific interpretation (which function to call for which header) is
// handled in the communication/serialization layer, not here.
class IdGenerator {
public:
    IdGenerator();

    // Returns a UUID v4 string with hyphens: "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx"
    // All hex digits are lowercase.  Version nibble = 4, variant bits = 10xx.
    [[nodiscard]] std::string uuid_v4();

    // Returns a UUID v4 string without hyphens: 32 lowercase hex characters.
    // Equivalent to Python's uuid.uuid4().hex.
    [[nodiscard]] std::string uuid_v4_without_hyphens();

    // Returns 32 random lowercase hex characters generated from 16 random bytes.
    // Not a UUID — no version or variant bits are set.
    // Equivalent to Python's secrets.token_hex(16).
    [[nodiscard]] std::string random_32_hex();

private:
    std::mt19937_64 rng_;

    void fill_random_bytes(std::uint8_t* buf, std::size_t n);
};

} // namespace edge_tts::common
