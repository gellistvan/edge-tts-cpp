#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace edge_tts::common::utf8 {

// ---- Constexpr byte-level helpers ----------------------------------------

// True for UTF-8 continuation bytes (0b10xxxxxx).
[[nodiscard]] constexpr bool is_continuation(char c) noexcept {
    return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u;
}

// Largest position p ≤ pos such that text[p] is the first byte of a code point.
// If pos ≥ text.size(), returns text.size().
// Kept for backward compatibility; prefer previous_code_point_boundary().
[[nodiscard]] constexpr std::size_t safe_boundary(
        std::string_view text, std::size_t pos) noexcept {
    if (pos >= text.size()) return text.size();
    while (pos > 0 && is_continuation(text[pos])) --pos;
    return pos;
}

// ---- Full validation -------------------------------------------------------

// Returns true iff text is well-formed UTF-8.
//
// Rejects:
//   - Invalid lead bytes (0x80–0xBF, 0xF5–0xFF)
//   - Missing or invalid continuation bytes (truncated sequences)
//   - Overlong encodings (e.g. 0xC0 0x80 for U+0000)
//   - Surrogate code points (U+D800–U+DFFF)
//   - Code points above U+10FFFF
[[nodiscard]] bool is_valid_utf8(std::string_view text) noexcept;

// ---- Boundary navigation --------------------------------------------------

// Returns the largest p ≤ index such that text[p] is the first byte of a
// code point (i.e., a non-continuation byte).  Returns 0 if every byte up to
// and including index is a continuation byte.
[[nodiscard]] std::size_t previous_code_point_boundary(
        std::string_view text, std::size_t index) noexcept;

// Returns the smallest p > index (or text.size()) that is the first byte of
// a code point.  Advances past the code point that starts at or before index.
[[nodiscard]] std::size_t next_code_point_boundary(
        std::string_view text, std::size_t index) noexcept;

// ---- Splitting ------------------------------------------------------------

// Splits text into the smallest number of string_view chunks such that each
// chunk is at most max_bytes bytes and no chunk ends inside a multi-byte
// code point.  The chunks are non-overlapping views into text.
// Throws std::invalid_argument if max_bytes == 0.
[[nodiscard]] std::vector<std::string_view> split_utf8_by_byte_limit(
        std::string_view text, std::size_t max_bytes);

} // namespace edge_tts::common::utf8
