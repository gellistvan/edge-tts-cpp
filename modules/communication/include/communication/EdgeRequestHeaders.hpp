#pragma once

#include "communication/EdgeServiceConfig.hpp"
#include "common/IdGenerator.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace edge_tts::communication {

// Canonical Edge TTS request header builders.
//
// All Edge TTS requests require a set of upgrade/request headers derived from
// the service constants plus a per-request MUID cookie.
//
// These two free functions are the single place in the C++ codebase that knows
// which headers to send for each endpoint.  All callers — SynthesisSession for
// WebSocket and VoiceService for HTTP — must use these builders rather than
// hard-coding header lists inline.
//

// ---------------------------------------------------------------------------
// WebSocket upgrade headers
// ---------------------------------------------------------------------------
//


//
// Header set (WSS_HEADERS merged with BASE_HEADERS + DRM.headers_with_muid()):
//   Pragma:          no-cache
//   Cache-Control:   no-cache
//   Origin:          <EdgeServiceConfig::origin>
//   User-Agent:      <EdgeServiceConfig::user_agent>
//   Accept-Encoding: gzip, deflate, br, zstd
//   Accept-Language: en-US,en;q=0.9
//   Cookie:          muid=<32 uppercase hex chars>;
//
// A fresh MUID (random 16 bytes, 32 uppercase hex chars) is generated per call.
// The returned type matches WebSocketClientOptions::extra_headers.
[[nodiscard]] std::vector<std::pair<std::string, std::string>>
build_websocket_headers(const EdgeServiceConfig& config,
                        common::IdGenerator&     ids);

// ---------------------------------------------------------------------------
// Voice-list HTTP request headers
// ---------------------------------------------------------------------------
//


//
// Header set (VOICE_HEADERS merged with BASE_HEADERS + DRM.headers_with_muid()):
//   User-Agent:      <EdgeServiceConfig::user_agent>
//   Accept-Encoding: gzip, deflate, br, zstd
//   Accept-Language: en-US,en;q=0.9
//   Accept:          */*
//   Cookie:          muid=<32 uppercase hex chars>;
//
// A fresh MUID is generated per call.
// The returned type matches HttpRequest::headers.
[[nodiscard]] std::map<std::string, std::string>
build_voice_list_headers(const EdgeServiceConfig& config,
                         common::IdGenerator&     ids);

} // namespace edge_tts::communication
