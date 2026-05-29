#pragma once

#include "edge_tts/common/Result.hpp"
#include "edge_tts/core/Voice.hpp"
#include "edge_tts/communication/EdgeServiceConfig.hpp"
#include "edge_tts/communication/IHttpClient.hpp"
#include "edge_tts/serialization/VoiceJsonParser.hpp"

#include <optional>
#include <string>
#include <vector>

namespace edge_tts::communication {

// Client-side filter applied after voice-list fetch and parse.
//
// All set fields are ANDed together.  An empty filter (all nullopt) returns
// every voice in wire order.
//
// Reference mapping:
//   locale     → Python VoicesManager.find(Locale=...)
//   gender     → Python VoicesManager.find(Gender=...)
//   short_name → exact ShortName match (not in Python VoicesManager, added for C++ API)
struct VoiceFilter {
    std::optional<std::string>          locale;
    std::optional<core::VoiceGender>    gender;
    std::optional<std::string>          short_name;
};

// Fetches and parses the Edge TTS voice list.
//
// Reference: voices.py list_voices() + __list_voices()
//   - HTTP GET to the voice-list endpoint.
//   - Headers: User-Agent and Accept from EdgeServiceConfig.
//   - Non-200 status → ErrorCode::service_error.
//   - 200 body parsed by VoiceJsonParser (delegation: no JSON in communication layer).
//   - Wire ordering preserved; sorting for CLI display is the caller's concern.
//   - VoiceFilter applied client-side after parsing.
//
// Note: Sec-MS-GEC DRM token is NOT added in this implementation — the base
// URL already contains trustedclienttoken and is sufficient for the offline
// test harness.  Real networking will inject the token via EdgeTokenProvider.
class VoiceService {
public:
    VoiceService(const EdgeServiceConfig&               config,
                 IHttpClient&                            http,
                 const serialization::VoiceJsonParser&  parser);

    [[nodiscard]] common::Result<std::vector<core::Voice>>
    list_voices(const VoiceFilter& filter = {});

private:
    const EdgeServiceConfig&              config_;
    IHttpClient&                          http_;
    const serialization::VoiceJsonParser& parser_;
};

} // namespace edge_tts::communication
