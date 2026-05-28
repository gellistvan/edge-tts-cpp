#pragma once

#include "edge_tts/common/Result.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace edge_tts::core {

// Matches the Python Voice TypedDict Gender field: "Female" | "Male".
// unknown is used when the field is absent or unrecognised (not a wire value).
enum class VoiceGender {
    unknown,
    female,
    male,
};

// Returns the wire-format string: "Female", "Male", or "Unknown".
// "Unknown" is never sent to the service; it is used only for logging.
[[nodiscard]] std::string_view to_string(VoiceGender gender) noexcept;

// Parses "Female" or "Male" (case-sensitive, matching the Python wire value).
// Returns an error for any other value including empty.
[[nodiscard]] common::Result<VoiceGender> voice_gender_from_string(
        std::string_view value);

// Represents one voice entry returned by the Edge TTS voice-list endpoint.
//
// Field mapping from the Python Voice TypedDict (typing.py):
//   Name              → name
//   ShortName         → short_name
//   Gender            → gender (VoiceGender enum)
//   Locale            → locale
//   SuggestedCodec    → suggested_codec
//   FriendlyName      → friendly_name
//   Status            → status  ("GA" | "Preview" | "Deprecated")
//   VoiceTag.ContentCategories   → content_categories
//   VoiceTag.VoicePersonalities  → voice_personalities
//   Language (VoicesManagerVoice) → language  (= locale prefix before first '-')
//
// All fields default to empty / unknown so the struct can be built incrementally
// by the JSON parser without requiring every field to be present.
struct Voice {
    std::string              name;                // full "Microsoft Server Speech..." form
    std::string              short_name;          // e.g. "en-US-EmmaMultilingualNeural"
    VoiceGender              gender{VoiceGender::unknown};
    std::string              locale;              // e.g. "en-US"
    std::string              friendly_name;
    std::string              status;              // "GA", "Preview", or "Deprecated"
    std::string              suggested_codec;     // e.g. "audio-24khz-48kbitrate-mono-mp3"
    std::vector<std::string> content_categories; // VoiceTag.ContentCategories
    std::vector<std::string> voice_personalities;// VoiceTag.VoicePersonalities
    std::string              language;           // Locale.split('-')[0], e.g. "en"

    [[nodiscard]] bool operator==(const Voice& other) const noexcept;
    [[nodiscard]] bool operator!=(const Voice& other) const noexcept;
};

} // namespace edge_tts::core
