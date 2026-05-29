#pragma once

#include <string>

namespace edge_tts::communication {

// All hard-coded constants for the Edge TTS service in one place.
//
// Reference files inspected:
//   reference/edge-tts/src/edge_tts/constants.py   — endpoints, token, version, headers
//   reference/edge-tts/src/edge_tts/communicate.py  — protocol paths, content types
//   reference/edge-tts/src/edge_tts/drm.py          — trusted client token usage
//   reference/edge-tts/src/edge_tts/voices.py       — voice list endpoint usage
//
// No networking or protocol code here — pure data.  All values are derived
// verbatim from the reference; see docs/PROTOCOL_NOTES.md for rationale.
struct EdgeServiceConfig {
    // --- Endpoints -----------------------------------------------------------
    // Base WebSocket URL (without dynamic params ConnectionId, Sec-MS-GEC, …).
    // Callers append: &ConnectionId=<uuid>&Sec-MS-GEC=<token>&Sec-MS-GEC-Version=<ver>
    // Reference: constants.py WSS_URL
    std::string websocket_endpoint;

    // Base voice-list URL (without dynamic Sec-MS-GEC params).
    // Callers append: &Sec-MS-GEC=<token>&Sec-MS-GEC-Version=<ver>
    // Reference: constants.py VOICE_LIST
    std::string voices_endpoint;

    // --- Authentication / DRM ------------------------------------------------
    // Reference: constants.py TRUSTED_CLIENT_TOKEN
    std::string trusted_client_token;

    // Reference: constants.py SEC_MS_GEC_VERSION = "1-{CHROMIUM_FULL_VERSION}"
    std::string sec_ms_gec_version;

    // --- Request headers -----------------------------------------------------
    // Reference: constants.py WSS_HEADERS["Origin"]
    std::string origin;

    // Reference: constants.py BASE_HEADERS["User-Agent"]
    std::string user_agent;

    // --- Protocol frame paths ------------------------------------------------
    // Reference: communicate.py send_command_request() / __stream()
    std::string speech_config_path;   // "speech.config"
    std::string ssml_path;            // "ssml"
    std::string audio_metadata_path;  // "audio.metadata"
    std::string turn_end_path;        // "turn.end"
};

// Returns a config struct populated with the values from the Python reference.
// Call once and share; the struct is cheap to copy.
[[nodiscard]] EdgeServiceConfig default_edge_service_config();

} // namespace edge_tts::communication
