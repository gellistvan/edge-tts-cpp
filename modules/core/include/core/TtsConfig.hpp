#pragma once

#include "common/Result.hpp"
#include "core/OutputFormat.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace edge_tts::core {

// Controls which boundary metadata events are requested from the service.
// Wire values: "WordBoundary" and "SentenceBoundary".
enum class BoundaryType {
    word,      // "WordBoundary"   — metadata per spoken word
    sentence,  // "SentenceBoundary" — metadata per sentence (default)
};

// Wire-format string for BoundaryType.
[[nodiscard]] std::string_view to_string(BoundaryType type) noexcept;

// Parses "WordBoundary" or "SentenceBoundary"; rejects anything else.
[[nodiscard]] common::Result<BoundaryType> boundary_type_from_string(
        std::string_view value);

// Full synthesis configuration.  Call validate_tts_config() to verify field
// constraints; voice is accepted in either the short locale form or the full
// "Microsoft Server Speech..." form (validation accepts both).
struct TtsConfig {
    std::string  voice        {"en-US-EmmaMultilingualNeural"};
    std::string  rate         {"+0%"};
    std::string  volume       {"+0%"};
    std::string  pitch        {"+0Hz"};
    OutputFormat output_format{OutputFormat::default_format()};
    BoundaryType boundary_type{BoundaryType::sentence};

    // Factory — returns a TtsConfig with all fields at their default values.
    [[nodiscard]] static TtsConfig defaults();

    // Legacy throw-based validation (normalises voice in-place).
    // Throws common::ConfigurationError on the first invalid field.
    // Prefer validate_tts_config() for new code.
    void validate();
};

// Validates all fields of config.  Accepts voice in short or full form.
// Returns an error describing the first invalid field (includes the field name
// and the bad value in the message/context).
[[nodiscard]] common::Result<void> validate_tts_config(const TtsConfig& config);

// Converts a short voice name ("en-US-EmmaMultilingualNeural") to the full
// "Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)"
// form expected by the Edge TTS WebSocket API.
// Returns nullopt if the input matches neither the short nor the full pattern.
[[nodiscard]] std::optional<std::string> normalize_voice_name(std::string_view voice);

} // namespace edge_tts::core
