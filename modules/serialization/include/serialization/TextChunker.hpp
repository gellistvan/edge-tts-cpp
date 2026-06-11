#pragma once

#include "common/Result.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace edge_tts::serialization {

struct TextChunkerOptions {
    // Maximum byte length for any yielded chunk.
    // Measured against the XML-escaped text when size_after_xml_escape is true
    // (reference default), or against the normalized raw text when false.
    std::size_t max_chunk_size = 4096;

    // When true: escape first, then apply the byte limit to the escaped text.
    //   Pipeline: escape(normalize(text)) → split_by_byte_length(…, 4096)
    // When false: split the normalized (pre-escape) text, then escape each chunk.
    //   The returned chunks may exceed max_chunk_size after escaping.
    bool size_after_xml_escape = true;

    // When true, prefer splitting at the last newline (\n) before the limit.
    bool prefer_sentence_boundary = true;

    // When true, fall back to the last space before the limit when no newline is found.
    bool prefer_word_boundary = true;
};

// Splits raw input text into service-safe XML-escaped chunks.
//
// Pipeline (size_after_xml_escape = true, reference mode):
//   1. Validate UTF-8 and replace incompatible control characters (TextNormalizer).
//   2. XML-escape the normalized text (xml_escape).
//   3. Split the escaped text by byte length using the reference algorithm:
//        a. Prefer the last \n before the limit (sentence boundary).
//        b. Fall back to the last space before the limit (word boundary).
//        c. Fall back to the last UTF-8 code-point boundary at or before the limit.
//        d. Adjust backward past any unterminated XML entity (&…;).
//      Each chunk is stripped of leading/trailing whitespace; empty chunks are dropped.
//
// Returned strings are XML-escaped and ready for embedding inside SSML <prosody> text.
class TextChunker {
public:
    // Throws std::invalid_argument if opts.max_chunk_size == 0.
    explicit TextChunker(TextChunkerOptions opts = {});

    [[nodiscard]] common::Result<std::vector<std::string>>
    chunk(std::string_view input) const;

private:
    TextChunkerOptions opts_;
};

} // namespace edge_tts::serialization
