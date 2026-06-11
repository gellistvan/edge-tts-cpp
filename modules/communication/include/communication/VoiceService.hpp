#pragma once

#include "common/IdGenerator.hpp"
#include "common/Result.hpp"
#include "core/Voice.hpp"
#include "communication/EdgeServiceConfig.hpp"
#include "communication/EdgeTokenProvider.hpp"
#include "communication/HttpTypes.hpp"
#include "communication/IHttpClient.hpp"
#include "serialization/VoiceJsonParser.hpp"

#include <optional>
#include <string>
#include <vector>

namespace edge_tts::communication {

// Client-side filter applied after voice-list fetch and parse.
//
// All set fields are ANDed together.  An empty filter (all nullopt) returns
// every voice in wire order.
struct VoiceFilter {
    std::optional<std::string>          locale;
    std::optional<core::VoiceGender>    gender;
    std::optional<std::string>          short_name;
};

// Fetches and parses the Edge TTS voice list.
//
//   - HTTP GET to the voice-list endpoint with fresh Sec-MS-GEC token.
//   - Headers: User-Agent, Accept-Encoding, Accept-Language, Accept, Cookie/MUID.
//   - Non-200 status → ErrorCode::service_error.
//   - On HTTP 403: adjust clock skew and retry once.
//   - 200 body parsed by VoiceJsonParser (no JSON in communication layer).
//   - Wire ordering preserved; sorting for CLI display is the caller's concern.
//   - VoiceFilter applied client-side after parsing.
class VoiceService {
public:
    VoiceService(const EdgeServiceConfig&               config,
                 IHttpClient&                           http,
                 const serialization::VoiceJsonParser&  parser,
                 common::IdGenerator&                   ids,
                 EdgeTokenProvider&                     tokens);

    [[nodiscard]] common::Result<std::vector<core::Voice>>
    list_voices(const VoiceFilter& filter = {});

private:
    // Build URL with a fresh DRM token and send the HTTP GET.
    [[nodiscard]] common::Result<HttpResponse> send_request();

    const EdgeServiceConfig&              config_;
    IHttpClient&                          http_;
    const serialization::VoiceJsonParser& parser_;
    common::IdGenerator&                  ids_;
    EdgeTokenProvider&                    tokens_;
};

} // namespace edge_tts::communication
