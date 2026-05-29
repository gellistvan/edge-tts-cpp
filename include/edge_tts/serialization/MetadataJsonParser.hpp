#pragma once

#include "edge_tts/common/Result.hpp"
#include "edge_tts/core/Chunk.hpp"

#include <string_view>
#include <vector>

namespace edge_tts::serialization {

// Parses the JSON body of an Edge TTS audio.metadata WebSocket text frame into
// a list of BoundaryChunk values.
//
// Reference: communicate.py Communicate.__parse_metadata()
//
// Wire JSON shape:
//   {
//     "Metadata": [
//       {
//         "Type": "WordBoundary" | "SentenceBoundary" | "SessionEnd",
//         "Data": {
//           "Offset":   <integer, 100 ns ticks>,
//           "Duration": <integer, 100 ns ticks>,
//           "text": {
//             "Text": "<xml-escaped string>",
//             ...
//           }
//         }
//       }
//     ]
//   }
//
// Behaviour:
//   - "WordBoundary" and "SentenceBoundary" items are parsed into BoundaryChunks.
//   - "SessionEnd" items are silently skipped (reference uses continue).
//   - Any other Type returns parse_error (reference raises UnknownResponse).
//   - Offset and Duration are returned as raw 100 ns ticks; offset compensation
//     is NOT applied here — that belongs in the communication layer.
//   - The "Text" field is XML-unescaped (reference: xml.sax.saxutils.unescape).
//   - An empty "Metadata" array or an all-SessionEnd array returns an empty vector.
//   - Missing required fields (Metadata, Type, Data, Offset, Duration, text.Text)
//     return parse_error.
//   - Malformed JSON returns parse_error.
class MetadataJsonParser {
public:
    [[nodiscard]] common::Result<std::vector<core::BoundaryChunk>>
    parse(std::string_view json) const;
};

} // namespace edge_tts::serialization
