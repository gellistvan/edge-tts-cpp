#pragma once

// Legacy type predating VoiceService. Production code uses VoiceService
// (injected with IHttpClient, VoiceJsonParser, IdGenerator). HttpVoiceService
// has no production callers and exists only for backward compatibility.

#include "edge_tts/core/Voice.hpp"

#include <vector>

namespace edge_tts::communication {

class HttpVoiceService final {
public:
    [[nodiscard]] std::vector<core::Voice> list_voices() const;
};

} // namespace edge_tts::communication
