#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace edge_tts::api {

// Transport options for a SpeechSynthesizer session.
//
// Kept separate from core::TtsConfig (speech content and voice settings) so
// network concerns do not pollute the speech configuration type.
//
// Defaults:
//   ws_connect_timeout = 10 s   — WebSocket upgrade handshake deadline
//   ws_read_timeout    = 60 s   — per-frame receive deadline
//   http_timeout       = 30 s   — voice-list HTTP request deadline
struct SynthesisOptions {
    // HTTP/HTTPS proxy URL.  Accepted and validated at the CLI/API layer.
    // Both transport backends (WebSocketClient, HttpClient) return
    // ErrorCode::unsupported at runtime if this is set — the ixwebsocket
    // library has no client-side proxy API.  Credentials in the URL
    // (user:pass@host) are sanitized before appearing in any error message.
    std::optional<std::string> proxy;

    // WebSocket upgrade connection timeout.
    std::chrono::milliseconds ws_connect_timeout{10'000};

    // WebSocket read timeout (per-frame).
    std::chrono::milliseconds ws_read_timeout{60'000};

    // HTTP timeout for voice-list requests.
    std::chrono::milliseconds http_timeout{30'000};
};

} // namespace edge_tts::api
