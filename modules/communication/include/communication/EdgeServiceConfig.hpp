#pragma once

#include <string>

namespace edge_tts::communication {

// All hard-coded constants for the Edge TTS service in one place.
//
// No networking or protocol code here — pure data.
// See docs/PROTOCOL_NOTES.md for the detailed protocol specification.
struct EdgeServiceConfig {
    // --- Endpoints -----------------------------------------------------------
    // Base WebSocket URL (without dynamic params ConnectionId, Sec-MS-GEC, …).
    // Callers append: &ConnectionId=<uuid>&Sec-MS-GEC=<token>&Sec-MS-GEC-Version=<ver>
    std::string websocket_endpoint;

    // Base voice-list URL (without dynamic Sec-MS-GEC params).
    // Callers append: &Sec-MS-GEC=<token>&Sec-MS-GEC-Version=<ver>
    std::string voices_endpoint;

    // --- Authentication / DRM ------------------------------------------------
    std::string trusted_client_token;
    std::string sec_ms_gec_version;

    // --- Request headers -----------------------------------------------------
    std::string origin;
    std::string user_agent;

    // --- Protocol frame paths ------------------------------------------------
    std::string speech_config_path;   // "speech.config"
    std::string ssml_path;            // "ssml"
    std::string audio_metadata_path;  // "audio.metadata"
    std::string turn_end_path;        // "turn.end"
};

// Returns the default Edge TTS service configuration.
// Call once and share; the struct is cheap to copy.
[[nodiscard]] EdgeServiceConfig default_edge_service_config();

} // namespace edge_tts::communication
