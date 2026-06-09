#pragma once

#include "common/Result.hpp"
#include "serialization/ProtocolMessage.hpp"

#include <string_view>

namespace edge_tts::serialization {

// Parses an Edge TTS WebSocket text frame into a ProtocolMessage.
//
//
// Frame format:
//   Name:Value\r\n
//   ...
//   \r\n
//   {body}
//
// Error conditions (all return ErrorCode::parse_error):
//   - Frame does not contain \r\n\r\n (missing separator).
//   - Any header line contains no ':' (malformed header).
//
// Empty header section is accepted (frame begins with \r\n\r\n).
// Empty body is accepted.
// Duplicate headers are preserved in wire order.
// Unknown header names are silently kept.
// LF-only frames (\n instead of \r\n) are NOT accepted — the reference
//   splits exclusively on \r\n. The \r\n\r\n separator will not be found
//   in an LF-only frame, so parse() returns a parse_error.
class ProtocolParser {
public:
    [[nodiscard]] common::Result<ProtocolMessage> parse(std::string_view frame) const;
};

} // namespace edge_tts::serialization
