#pragma once

#include "edge_tts/common/Result.hpp"
#include "edge_tts/core/Voice.hpp"

#include <string_view>
#include <vector>

namespace edge_tts::serialization {

// Parses the JSON voice-list payload returned by the Edge TTS voice-list endpoint.
//
// Wire format (reference: voices.py list_voices(), typing.py Voice TypedDict):
//   JSON array of objects, each with:
//     Required: Name, ShortName, Gender, Locale, SuggestedCodec, FriendlyName, Status
//     Optional: VoiceTag (object)
//       VoiceTag.ContentCategories  → []  if absent
//       VoiceTag.VoicePersonalities → []  if absent
//
// Behaviour:
//   - Rejects malformed JSON (parse_error).
//   - Rejects non-array root (parse_error).
//   - Rejects entries with missing required fields (parse_error).
//   - Defaults VoiceTag and its sub-lists if absent, matching reference voices.py.
//   - Ignores unknown fields.
//   - Preserves ordering from the wire — callers sort if needed (reference CLI sorts
//     by ShortName only for display, not for storage).
//   - Derives Voice::language from Locale.split('-')[0].
//   - Converts Gender string to VoiceGender enum; rejects unrecognised values.
class VoiceJsonParser {
public:
    [[nodiscard]] common::Result<std::vector<core::Voice>>
    parse(std::string_view json) const;
};

} // namespace edge_tts::serialization
