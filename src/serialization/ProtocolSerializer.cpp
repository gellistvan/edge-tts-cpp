#include "edge_tts/serialization/ProtocolSerializer.hpp"

#include <string>

namespace edge_tts::serialization {

std::string ProtocolSerializer::serialize(const ProtocolMessage& message) const
{
    std::string out;
    // Estimate: each header is ~32 bytes + body.
    out.reserve(message.headers.size() * 32 + 4 + message.body.size());

    for (const auto& [name, value] : message.headers) {
        out += name;
        out += ':';
        out += value;
        out += "\r\n";
    }
    out += "\r\n";
    out += message.body;
    return out;
}

} // namespace edge_tts::serialization
