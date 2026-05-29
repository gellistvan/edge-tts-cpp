#include "edge_tts/serialization/ProtocolMessage.hpp"

#include <optional>
#include <string_view>

namespace edge_tts::serialization {

std::optional<std::string>
ProtocolMessage::header(std::string_view name) const
{
    for (const auto& [k, v] : headers)
        if (k == name) return v;
    return std::nullopt;
}

} // namespace edge_tts::serialization
