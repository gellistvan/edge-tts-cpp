#include "edge_tts/communication/VoiceService.hpp"
#include "edge_tts/communication/EdgeRequestHeaders.hpp"
#include "edge_tts/common/Error.hpp"
#include "edge_tts/communication/HttpTypes.hpp"
#include "edge_tts/core/Voice.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace edge_tts::communication {

VoiceService::VoiceService(const EdgeServiceConfig&              config,
                           IHttpClient&                           http,
                           const serialization::VoiceJsonParser&  parser,
                           common::IdGenerator&                   ids)
    : config_(config)
    , http_(http)
    , parser_(parser)
    , ids_(ids)
{}

common::Result<std::vector<core::Voice>>
VoiceService::list_voices(const VoiceFilter& filter)
{
    // Build request — reference: voices.py __list_voices()
    // The base voices_endpoint already contains trustedclienttoken.
    // Sec-MS-GEC is omitted here; it will be added by a higher layer
    // once EdgeTokenProvider integration is wired in.
    HttpRequest req;
    req.method  = "GET";
    req.url     = config_.voices_endpoint;
    req.headers = build_voice_list_headers(config_, ids_);

    // Send request
    auto resp = http_.send(req);
    if (!resp)
        return common::Result<std::vector<core::Voice>>::fail(resp.error());

    // Non-200 → service_error (reference: raise_for_status=True)
    if (resp->status_code != 200)
        return common::Result<std::vector<core::Voice>>::fail(
            {common::ErrorCode::service_error,
             "voice list HTTP request failed",
             std::to_string(resp->status_code)});

    // Delegate JSON parsing to serialization layer (no JSON in communication).
    auto voices = parser_.parse(resp->body);
    if (!voices)
        return common::Result<std::vector<core::Voice>>::fail(voices.error());

    // Apply client-side filter (all conditions ANDed).
    // Wire order is preserved — sorting for display is the caller's concern.
    if (filter.locale || filter.gender || filter.short_name) {
        auto& v = *voices;
        v.erase(std::remove_if(v.begin(), v.end(),
            [&](const core::Voice& voice) {
                if (filter.locale     && voice.locale      != *filter.locale)     return true;
                if (filter.gender     && voice.gender      != *filter.gender)     return true;
                if (filter.short_name && voice.short_name  != *filter.short_name) return true;
                return false;
            }),
        v.end());
    }

    return common::Result<std::vector<core::Voice>>::ok(std::move(*voices));
}

} // namespace edge_tts::communication
