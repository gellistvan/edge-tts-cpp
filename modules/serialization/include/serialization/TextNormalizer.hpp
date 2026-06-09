#pragma once

#include "common/Result.hpp"

#include <string>
#include <string_view>

namespace edge_tts::serialization {

// Normalizes raw text for safe passage into the Edge TTS pipeline.
//
//
// Normalisation steps (in order):
//   1. Validate UTF-8.  Returns an error for any invalid byte sequence.
//   2. Replace control characters U+0000–U+0008, U+000B–U+000C, U+000E–U+001F
//      with ASCII space (0x20).
//
// Preserved characters:
//   U+0009  HT  (tab)             — kept
//   U+000A  LF  (newline)         — kept
//   U+000D  CR  (carriage return) — kept (CRLF is NOT normalised to LF)
//   U+0020+ printable ASCII       — kept
//   All valid multi-byte sequences — kept
//
// The function does NOT trim leading/trailing whitespace.  That is done
// per-chunk by the text chunker.
class TextNormalizer {
public:
    [[nodiscard]] common::Result<std::string> normalize(std::string_view input) const;
};

} // namespace edge_tts::serialization
