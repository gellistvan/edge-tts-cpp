#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace edge_tts::serialization {

// Represents one Edge TTS WebSocket text frame, split into headers and body.
//
// Wire format:
//
//   Name:Value\r\n
//   Name:Value\r\n
//   \r\n
//   {body}
//
// Header name/value are separated by the first ':' — values may contain colons.
// All separators use CRLF (\r\n).  The header/body boundary is \r\n\r\n.
// Header ordering is preserved; duplicate headers are allowed in both
// directions (last value wins when looked up by name).
//
// The parser preserves all headers in wire order; duplicate names are allowed.
struct ProtocolMessage {
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    // Returns the value of the first header with the given name, or nullopt.
    // Comparison is case-sensitive.
    // Returns a copy so callers can safely hold the result past the message's lifetime.
    [[nodiscard]] std::optional<std::string> header(std::string_view name) const;
};

} // namespace edge_tts::serialization
