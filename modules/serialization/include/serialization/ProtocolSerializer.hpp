#pragma once

#include "serialization/ProtocolMessage.hpp"

#include <string>

namespace edge_tts::serialization {

// Serializes a ProtocolMessage into an Edge TTS WebSocket text frame string.
//
//
// Output format:
//   Name:Value\r\n        (one line per header, no space after ':')
//   \r\n                  (header/body separator — empty line)
//   {body}                (body appended verbatim, may be empty)
//
// Header order is preserved from ProtocolMessage::headers.
class ProtocolSerializer {
public:
    [[nodiscard]] std::string serialize(const ProtocolMessage& message) const;
};

} // namespace edge_tts::serialization
