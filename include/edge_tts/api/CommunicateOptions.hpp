#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace edge_tts::api {

// Runtime / transport options for a Communicate session.
//
// These settings are deliberately separate from core::TtsConfig, which is
// speech-only.  Proxy, connection timeout, and read timeout are transport
// concerns that must not pollute the speech configuration type.
//
// Reference: communicate.py Communicate.__init__() — proxy, communicate.py
// __stream() — sock_connect / sock_read timeouts via aiohttp.ClientSession.
//
// Default values mirror the Python reference:
//   ws_connect_timeout = 10 s  (sock_connect=10 in aiohttp ClientSession)
//   ws_read_timeout    = 60 s  (sock_read=60 in aiohttp ClientSession)
//   http_timeout       = 30 s  (reasonable default for voice-list GET)
struct CommunicateOptions {
    // HTTP/HTTPS proxy URL.  Accepted and validated at the CLI/API layer.
    // Both transport backends (WebSocketClient, HttpClient) return
    // ErrorCode::unsupported at runtime if this is set — the ixwebsocket
    // library has no client-side proxy API.  Credentials in the URL
    // (user:pass@host) are sanitized before appearing in any error message.
    // Reference: communicate.py proxy parameter, aiohttp trust_env / proxy kwarg.
    std::optional<std::string> proxy;

    // WebSocket upgrade connection timeout.
    // Reference: aiohttp.ClientSession(connector=TCPConnector(..., sock_connect=10))
    std::chrono::milliseconds ws_connect_timeout{10'000};

    // WebSocket read timeout (per-frame).
    // Reference: aiohttp.ClientSession(connector=TCPConnector(..., sock_read=60))
    std::chrono::milliseconds ws_read_timeout{60'000};

    // HTTP timeout for voice-list requests.
    // No direct Python reference timeout; 30 s is the chosen C++ default.
    std::chrono::milliseconds http_timeout{30'000};
};

} // namespace edge_tts::api
