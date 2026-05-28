#pragma once

#include "edge_tts/common/Result.hpp"
#include "edge_tts/core/TtsConfig.hpp"

#include <string>
#include <string_view>

namespace edge_tts::serialization {

// Builds a single SSML document for one text chunk, matching the exact string
// produced by the Python reference mkssml() in communicate.py.
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
//   build() validates the TtsConfig and propagates any error before building.
//   Callers may pre-validate with validate_tts_config() to avoid the cost on
//   hot paths; build() always re-validates so the SSML is never malformed.
//
// Text processing:
//   raw_text is normalized (TextNormalizer) and XML-escaped (xml_escape) exactly
//   once inside build().  Do not pass already-escaped text — it will be
//   double-escaped.
//
// Scope:
//   build() produces only the SSML document body.  WebSocket framing and
//   X-RequestId / X-Timestamp protocol headers are NOT included — those belong
//   in the communication layer (EdgeProtocol).
class SsmlBuilder {
public:
    [[nodiscard]] common::Result<std::string> build(
        const core::TtsConfig& config,
        std::string_view       raw_text) const;
};

} // namespace edge_tts::serialization
