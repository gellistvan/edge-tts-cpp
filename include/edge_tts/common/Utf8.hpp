#pragma once

#include <cstddef>
#include <string_view>

namespace edge_tts::common::utf8 {

// Returns true for UTF-8 continuation bytes (0b10xxxxxx).
// Used to detect mid-sequence byte positions.
[[nodiscard]] constexpr bool is_continuation(char c) noexcept {
    return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u;
}

// Returns the largest position p ≤ pos such that text[p] is the first byte of
// a UTF-8 code point (or 0 if pos == 0).  Use this before slicing text so the
// cut never falls inside a multi-byte sequence.
[[nodiscard]] constexpr std::size_t safe_boundary(
    std::string_view text, std::size_t pos) noexcept
{
    if (pos >= text.size()) return text.size();
    while (pos > 0 && is_continuation(text[pos])) --pos;
    return pos;
}

// Returns true when the entire string is well-formed UTF-8.
[[nodiscard]] constexpr bool is_valid(std::string_view text) noexcept {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto byte = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        if (byte < 0x80u) {
            extra = 0;
        } else if ((byte & 0xE0u) == 0xC0u) {
            extra = 1;
        } else if ((byte & 0xF0u) == 0xE0u) {
            extra = 2;
        } else if ((byte & 0xF8u) == 0xF0u) {
            extra = 3;
        } else {
            return false;  // invalid lead byte
        }
        ++i;
        for (std::size_t j = 0; j < extra; ++j, ++i) {
            if (i >= text.size() || !is_continuation(text[i])) return false;
        }
    }
    return true;
}

} // namespace edge_tts::common::utf8
