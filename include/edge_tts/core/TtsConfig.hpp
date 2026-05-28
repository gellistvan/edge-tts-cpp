#pragma once

#include "edge_tts/core/Chunk.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace edge_tts::core {

// Configuration for a single TTS synthesis request.
//
// Voice accepts either the short locale form ("en-US-EmmaMultilingualNeural")
// or the full "Microsoft Server Speech Text to Speech Voice (locale, name)"
// form.  Call validate() to normalize the voice field in-place and verify all
// fields match the Edge TTS wire format constraints.
struct TtsConfig {
    std::string  voice{"en-US-EmmaMultilingualNeural"};
    std::string  rate{"+0%"};
    std::string  volume{"+0%"};
    std::string  pitch{"+0Hz"};
    BoundaryType boundary{BoundaryType::SentenceBoundary};

    // Normalizes voice to the full "Microsoft Server Speech..." form and
    // validates rate/volume/pitch syntax.  Throws ConfigurationError if any
    // field is invalid.  Safe to call multiple times (idempotent).
    void validate();
};

// Converts a short voice name ("en-US-EmmaMultilingualNeural") to the full
// "Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)"
// form expected by the Edge TTS WebSocket API.
// Returns nullopt if the input matches neither the short nor the full pattern.
[[nodiscard]] std::optional<std::string> normalize_voice_name(std::string_view voice);

} // namespace edge_tts::core
