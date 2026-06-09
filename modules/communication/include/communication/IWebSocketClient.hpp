#pragma once

#include "common/Result.hpp"
#include "communication/WebSocketMessage.hpp"

#include <string_view>

namespace edge_tts::communication {

// Minimal synchronous WebSocket transport boundary.
//
//
// Error model:
//   Every operation returns Result<> so errors propagate without exceptions.
//   Transport-level failures use ErrorCode::network_error.
//   Protocol violations detected by the transport use ErrorCode::protocol_error.
class IWebSocketClient {
public:
    virtual ~IWebSocketClient() = default;

    // Open a WebSocket connection to the given URL.
    // Must be called before send_text() or receive().
    [[nodiscard]] virtual common::Result<void> connect(std::string_view url) = 0;

    // Send a UTF-8 text frame.
    [[nodiscard]] virtual common::Result<void> send_text(std::string_view payload) = 0;

    // Receive the next incoming message (text or binary).
    // Blocks until a message is available or an error occurs.
    [[nodiscard]] virtual common::Result<WebSocketMessage> receive() = 0;

    // Close the WebSocket connection.
    [[nodiscard]] virtual common::Result<void> close() = 0;
};

} // namespace edge_tts::communication
