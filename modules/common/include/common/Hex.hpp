#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace edge_tts::common {

// Returns the lowercase hex encoding of bytes, e.g. {0xde, 0xad} → "dead".
[[nodiscard]] std::string hex_encode_lower(std::span<const std::byte> bytes);

// Returns the uppercase hex encoding of bytes, e.g. {0xde, 0xad} → "DEAD".
[[nodiscard]] std::string hex_encode_upper(std::span<const std::byte> bytes);

// Returns true iff every character of value is a valid hex digit
// (0-9, a-f, A-F) and the string is non-empty.
[[nodiscard]] bool is_hex(std::string_view value) noexcept;

} // namespace edge_tts::common
