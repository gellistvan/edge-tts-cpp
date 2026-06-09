#include "common/Hex.hpp"

namespace edge_tts::common {

namespace {

constexpr char LOWER[] = "0123456789abcdef";
constexpr char UPPER[] = "0123456789ABCDEF";

std::string encode(std::span<const std::byte> bytes, const char* digits) {
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto b : bytes) {
        const auto v = static_cast<unsigned char>(b);
        out += digits[v >> 4];
        out += digits[v & 0x0fu];
    }
    return out;
}

} // namespace

std::string hex_encode_lower(std::span<const std::byte> bytes) {
    return encode(bytes, LOWER);
}

std::string hex_encode_upper(std::span<const std::byte> bytes) {
    return encode(bytes, UPPER);
}

bool is_hex(std::string_view value) noexcept {
    if (value.empty()) return false;
    for (const char c : value) {
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

} // namespace edge_tts::common
