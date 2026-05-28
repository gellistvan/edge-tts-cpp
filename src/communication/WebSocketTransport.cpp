#include "edge_tts/communication/WebSocketTransport.hpp"
#include "edge_tts/common/Errors.hpp"

namespace edge_tts::communication {

void WebSocketTransport::connect(const std::string&) {
    throw common::NetworkError{"WebSocket transport is not implemented yet"};
}

void WebSocketTransport::send_text(const std::string&) {
    throw common::NetworkError{"WebSocket transport is not implemented yet"};
}

RawMessage WebSocketTransport::receive() {
    throw common::NetworkError{"WebSocket transport is not implemented yet"};
}

void WebSocketTransport::close() {}

} // namespace edge_tts::communication
