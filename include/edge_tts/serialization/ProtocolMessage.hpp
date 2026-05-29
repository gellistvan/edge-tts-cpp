#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace edge_tts::serialization {

// Represents one Edge TTS WebSocket text frame, split into headers and body.
//
// Wire format (reference: communicate.py ssml_headers_plus_data / send_command_request):
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
// Note: the Python reference uses a dict for parsed headers which means
// duplicate header names cause the earlier value to be silently overwritten.
// The C++ parser preserves all headers in wire order.
struct ProtocolMessage {
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    // Returns the value of the first header with the given name, or nullopt.
    // Comparison is case-sensitive, matching the Python reference behaviour.
    // Returns a copy so callers can safely hold the result past the message's lifetime.
    [[nodiscard]] std::optional<std::string> header(std::string_view name) const;
};

} // namespace edge_tts::serialization
