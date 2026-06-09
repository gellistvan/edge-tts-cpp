#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace edge_tts::communication {

// A raw incoming WebSocket message, classified by frame type.
//
//
// The parser only deals with text and binary; ERROR frames are surfaced by
// the transport layer before calling parse_incoming().
struct WebSocketMessage {
    enum class Type { text, binary };

    Type type;
    std::string            text;    // populated when type == text
    std::vector<std::byte> binary;  // populated when type == binary
};

} // namespace edge_tts::communication
