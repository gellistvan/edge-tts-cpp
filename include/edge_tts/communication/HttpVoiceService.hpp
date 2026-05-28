#pragma once

#include "edge_tts/core/Voice.hpp"

#include <vector>

namespace edge_tts::communication {

class HttpVoiceService final {
public:
    [[nodiscard]] std::vector<core::Voice> list_voices() const;
};

} // namespace edge_tts::communication
