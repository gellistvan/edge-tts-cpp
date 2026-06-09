#pragma once
// Shared frame-building helpers for tests that operate at the WebSocket
// protocol layer.  Included by both tests/communication/ and tests/api/ suites.
//
// All helpers are in namespace edge_tts::test to avoid polluting the global
// namespace of including files.  Use 'using namespace edge_tts::test;' or
// individual 'using edge_tts::test::make_turn_end;' declarations as needed.

#include "communication/WebSocketMessage.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace edge_tts::test {

using communication::WebSocketMessage;

// Convert a string to a byte vector (for passing string literals to the binary
// frame builder without a string overload at every call site).
inline std::vector<std::byte> to_bytes(const std::string& s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (char c : s)
        out.push_back(static_cast<std::byte>(c));
    return out;
}

// Build a binary audio frame with the Edge TTS wire format:
//   [2 bytes big-endian header-length][header bytes][CRLF][body bytes]
inline WebSocketMessage make_audio_frame(const std::vector<std::byte>& body) {
    const std::string hdr = "X-RequestId:abc\r\nPath:audio\r\nContent-Type:audio/mpeg";
    const auto hl = static_cast<uint16_t>(2 + hdr.size());
    std::vector<std::byte> frame;
    frame.reserve(2 + hdr.size() + 2 + body.size());
    frame.push_back(static_cast<std::byte>(hl >> 8));
    frame.push_back(static_cast<std::byte>(hl & 0xff));
    for (char c : hdr)  frame.push_back(static_cast<std::byte>(c));
    frame.push_back(static_cast<std::byte>('\r'));
    frame.push_back(static_cast<std::byte>('\n'));
    for (auto b : body) frame.push_back(b);
    WebSocketMessage m;
    m.type   = WebSocketMessage::Type::binary;
    m.binary = std::move(frame);
    return m;
}

inline WebSocketMessage make_audio_frame(const std::string& body) {
    return make_audio_frame(to_bytes(body));
}

// Build a text turn.end frame.
inline WebSocketMessage make_turn_end() {
    WebSocketMessage m;
    m.type = WebSocketMessage::Type::text;
    m.text = "X-RequestId:abc\r\nPath:turn.end\r\n\r\n";
    return m;
}

// Build a word-boundary audio.metadata frame.
inline WebSocketMessage make_word_boundary(int64_t offset_ticks,
                                           int64_t duration_ticks,
                                           const std::string& word) {
    WebSocketMessage m;
    m.type = WebSocketMessage::Type::text;
    m.text = "X-RequestId:abc\r\nPath:audio.metadata\r\n\r\n"
             "{\"Metadata\":[{\"Type\":\"WordBoundary\","
             "\"Data\":{\"Offset\":" + std::to_string(offset_ticks) +
             ",\"Duration\":" + std::to_string(duration_ticks) +
             ",\"text\":{\"Text\":\"" + word + "\"}}}]}";
    return m;
}

} // namespace edge_tts::test
