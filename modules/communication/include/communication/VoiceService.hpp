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
//   - URL: config.voices_endpoint + &Sec-MS-GEC=<token>&Sec-MS-GEC-Version=<ver>
//     (reference: voices.py f"{VOICE_LIST}&Sec-MS-GEC={DRM.generate_sec_ms_gec()}&...")
//   - Headers: build_voice_list_headers() (User-Agent, Accept-Encoding,
//     Accept-Language, Accept, Cookie/MUID).
//   - Non-200 status → ErrorCode::service_error.
//   - On HTTP 403: adjust clock skew and retry once (matching voices.py).
//   - 200 body parsed by VoiceJsonParser (delegation: no JSON in communication layer).
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
