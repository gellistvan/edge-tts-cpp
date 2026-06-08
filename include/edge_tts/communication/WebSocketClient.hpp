#pragma once

#include "edge_tts/common/Result.hpp"
#include "edge_tts/communication/IWebSocketClient.hpp"
#include "edge_tts/communication/WebSocketMessage.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace edge_tts::communication {

// Transport-level options for WebSocketClient.
//
// Reference: communicate.py __stream() — aiohttp.ClientSession.ws_connect()
//   connect_timeout → sock_connect timeout (default 10 s in Python reference)
//   read_timeout    → sock_read timeout (default 60 s in Python reference)
//   proxy           → accepted but not functional; connect() returns
//                     ErrorCode::unsupported if set — ixwebsocket has no
//                     CONNECT-tunnel proxy API.
//   extra_headers   → WSS_HEADERS (Pragma, Cache-Control, Origin, User-Agent, …)
//
// extra_headers is set by the caller (e.g. SynthesisSession) before construction
// so that the same client instance always uses the same upgrade headers.
struct WebSocketClientOptions {
    std::optional<std::string>                       proxy;
    std::chrono::milliseconds                        connect_timeout{10'000};
    std::chrono::milliseconds                        read_timeout{60'000};
    std::vector<std::pair<std::string, std::string>> extra_headers;
};

// Real WebSocket client backed by ixwebsocket (IXWebSocket submodule).
//
// connect() establishes a synchronous TLS WebSocket connection. After a
// successful connect() the client starts an internal receive thread that pumps
// incoming frames into a blocking queue. receive() pops from that queue,
// blocking until a frame arrives or read_timeout elapses.
//
// Thread safety: send_text() and close() are safe to call concurrently with
// a blocking receive(). close() is idempotent.
//
// No ixwebsocket types appear in this header — they live entirely in the .cpp
// behind the Pimpl idiom.
class WebSocketClient final : public IWebSocketClient {
public:
    explicit WebSocketClient(WebSocketClientOptions options = {});
    ~WebSocketClient() override;

    WebSocketClient(const WebSocketClient&)            = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;
    WebSocketClient(WebSocketClient&&)                 = delete;
    WebSocketClient& operator=(WebSocketClient&&)      = delete;

    // Open a TLS WebSocket connection to url.
    // Applies extra_headers as upgrade request headers.
    // Returns network_error on transport failure.
    [[nodiscard]] common::Result<void> connect(std::string_view url) override;

    // Send a UTF-8 text frame.
    // Returns network_error if the connection is closed or the send fails.
    [[nodiscard]] common::Result<void> send_text(std::string_view payload) override;

    // Block until the next incoming frame arrives or read_timeout elapses.
    // Returns timeout on deadline expiry, network_error on connection failure.
    [[nodiscard]] common::Result<WebSocketMessage> receive() override;

    // Close the WebSocket connection and stop the receive thread.
    // Idempotent: safe to call multiple times.
    [[nodiscard]] common::Result<void> close() override;

    // Accessor — primarily for tests.
    [[nodiscard]] const WebSocketClientOptions& options() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace edge_tts::communication
