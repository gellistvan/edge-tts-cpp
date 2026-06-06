#pragma once

// SCHEDULED FOR REMOVAL.
//
// HttpVoiceService is an empty stub that predates VoiceService.  Production
// code now uses communication::VoiceService (which takes IHttpClient,
// VoiceJsonParser, and IdGenerator) via apps/edge-tts/main.cpp.
// HttpVoiceService has no callers and will be deleted in a future cleanup.

#include "edge_tts/core/Voice.hpp"

#include <vector>

namespace edge_tts::communication {

// Scheduled for removal — VoicesManager is the only caller and is itself unused.
class HttpVoiceService final {
public:
    [[nodiscard]] std::vector<core::Voice> list_voices() const;
};

} // namespace edge_tts::communication
