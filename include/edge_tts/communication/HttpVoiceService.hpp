#pragma once

// Legacy type predating VoiceService. Production code uses VoiceService
// (injected with IHttpClient, VoiceJsonParser, IdGenerator, EdgeTokenProvider).
// HttpVoiceService has no production callers and is retained only because
// VoicesManager depends on it; both are deprecated.

#include "edge_tts/core/Voice.hpp"

#include <vector>

namespace edge_tts::communication {

class [[deprecated("Use VoiceService with EdgeTokenProvider injection instead")]]
HttpVoiceService final {
public:
    [[nodiscard]] std::vector<core::Voice> list_voices() const;
};

} // namespace edge_tts::communication
