#include "common/Utf8.hpp"

#include <cstdint>
#include <stdexcept>

namespace edge_tts::common::utf8 {

bool is_valid_utf8(std::string_view text) noexcept {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto b0 = static_cast<std::uint8_t>(text[i]);

        std::uint32_t codepoint = 0;
        std::size_t   extra     = 0;
        std::uint32_t min_cp    = 0;

        if (b0 < 0x80u) {
            ++i;
            continue;
        } else if ((b0 & 0xE0u) == 0xC0u) {
            if (b0 < 0xC2u) return false;  // overlong: 0xC0 / 0xC1
            extra  = 1;
            min_cp = 0x80u;
            codepoint = b0 & 0x1Fu;
        } else if ((b0 & 0xF0u) == 0xE0u) {
            extra  = 2;
            min_cp = 0x800u;
            codepoint = b0 & 0x0Fu;
        } else if ((b0 & 0xF8u) == 0xF0u) {
            if (b0 > 0xF4u) return false;  // would exceed U+10FFFF
            extra  = 3;
            min_cp = 0x10000u;
            codepoint = b0 & 0x07u;
        } else {
            return false;  // continuation byte as lead, or 0xF5–0xFF
        }

        for (std::size_t j = 0; j < extra; ++j) {
            ++i;
            if (i >= text.size()) return false;  // truncated sequence
            const auto b = static_cast<std::uint8_t>(text[i]);
            if ((b & 0xC0u) != 0x80u) return false;  // invalid continuation
            codepoint = (codepoint << 6) | (b & 0x3Fu);
        }
        ++i;

        if (codepoint < min_cp)               return false;  // overlong
        if (codepoint >= 0xD800u &&
            codepoint <= 0xDFFFu)             return false;  // UTF-16 surrogate
        if (codepoint > 0x10FFFFu)            return false;  // above Unicode max
    }
    return true;
}

std::size_t previous_code_point_boundary(
        std::string_view text, std::size_t index) noexcept {
    return safe_boundary(text, index);
}

std::size_t next_code_point_boundary(
        std::string_view text, std::size_t index) noexcept {
    if (index >= text.size()) return text.size();
    // Advance past the current lead byte.
    ++index;
    // Skip any continuation bytes that follow.
    while (index < text.size() && is_continuation(text[index])) ++index;
    return index;
}

std::vector<std::string_view> split_utf8_by_byte_limit(
        std::string_view text, std::size_t max_bytes) {
    if (max_bytes == 0)
        throw std::invalid_argument{"split_utf8_by_byte_limit: max_bytes must be > 0"};

    std::vector<std::string_view> result;
    std::size_t pos = 0;

    while (pos < text.size()) {
        const std::size_t remaining = text.size() - pos;
        if (remaining <= max_bytes) {
            result.push_back(text.substr(pos));
            break;
        }

        // Find the safe split boundary at or before pos + max_bytes.
        std::size_t end = previous_code_point_boundary(text, pos + max_bytes);

        if (end <= pos) {
            // The single code point starting at pos is longer than max_bytes
            // (shouldn't happen with valid UTF-8 and max_bytes ≥ 4, but be safe).
            // Force-advance past one code point to avoid an infinite loop.
            end = next_code_point_boundary(text, pos);
        }

        result.push_back(text.substr(pos, end - pos));
        pos = end;
    }

    return result;
}

} // namespace edge_tts::common::utf8
