#pragma once

#include "edge_tts/common/Result.hpp"
#include "edge_tts/core/TtsConfig.hpp"

#include <string>
#include <string_view>

namespace edge_tts::serialization {

// Builds a single SSML document for one text chunk.
//
// Reference template (all on one line, single-quoted attributes):
//   <speak version='1.0'
//          xmlns='http://www.w3.org/2001/10/synthesis'
//          xml:lang='en-US'>
//     <voice name='{voice}'>
//       <prosody pitch='{pitch}' rate='{rate}' volume='{volume}'>
//         {escaped_text}
//       </prosody>
//     </voice>
//   </speak>
//
// Validation policy:
//   Both entry points validate TtsConfig before building.
//
// Text processing — TWO entry points, one escaping contract each:
//
//   build(config, raw_text)
//     raw_text is normalized (TextNormalizer) and XML-escaped (xml_escape)
//     exactly once.  Pass raw user text here.  Do NOT pass already-escaped
//     text — it will be double-escaped (&amp; → &amp;amp;).
//
//   build_from_escaped_text(config, escaped_text)
//     escaped_text is embedded verbatim — no normalization, no XML-escaping.
//     Use when the caller already holds XML-escaped text, e.g. output from
//     serialization::TextChunker.  Do NOT pass raw user text — XML special
//     characters will be embedded literally and produce malformed SSML.
//
// Scope:
//   Both entry points produce only the SSML document body.  WebSocket framing
//   and X-RequestId / X-Timestamp protocol headers are NOT included — those
//   belong in the communication layer (EdgeProtocol).
class SsmlBuilder {
public:
    // Raw text path: normalize + XML-escape + assemble SSML.
    // Use for user-supplied text that has not yet been escaped.
    [[nodiscard]] common::Result<std::string> build(
        const core::TtsConfig& config,
        std::string_view       raw_text) const;

    // Pre-escaped text path: assemble SSML embedding escaped_text verbatim.
    // Use when the text was already XML-escaped, e.g. by TextChunker.
    // Calling this with raw text will produce malformed SSML.
    [[nodiscard]] common::Result<std::string> build_from_escaped_text(
        const core::TtsConfig& config,
        std::string_view       escaped_text) const;
};

} // namespace edge_tts::serialization
