#include "edge_tts/communication/VoiceService.hpp"
#include "edge_tts/communication/EdgeRequestHeaders.hpp"
#include "edge_tts/communication/EdgeTokenProvider.hpp"
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
                           common::IdGenerator&                   ids,
                           EdgeTokenProvider&                     tokens)
    : config_(config)
    , http_(http)
    , parser_(parser)
    , ids_(ids)
    , tokens_(tokens)
{}

// One attempt: build URL with fresh DRM token, send, parse.
common::Result<std::vector<core::Voice>> VoiceService::fetch_and_parse()
{
    auto token = tokens_.sec_ms_gec();
    if (!token)
        return common::Result<std::vector<core::Voice>>::fail(token.error());

    // Reference: voices.py __list_voices():
    //   f"{VOICE_LIST}&Sec-MS-GEC={DRM.generate_sec_ms_gec()}&Sec-MS-GEC-Version={SEC_MS_GEC_VERSION}"
    HttpRequest req;
    req.method  = "GET";
    req.url     = config_.voices_endpoint
                  + "&Sec-MS-GEC=" + *token
                  + "&Sec-MS-GEC-Version=" + tokens_.sec_ms_gec_version();
    req.headers = build_voice_list_headers(config_, ids_);

    auto resp = http_.send(req);
    if (!resp)
        return common::Result<std::vector<core::Voice>>::fail(resp.error());

    if (resp->status_code != 200)
        return common::Result<std::vector<core::Voice>>::fail(
            {common::ErrorCode::service_error,
             "voice list HTTP request failed",
             std::to_string(resp->status_code)});

    auto voices = parser_.parse(resp->body);
    if (!voices)
        return common::Result<std::vector<core::Voice>>::fail(voices.error());

    return common::Result<std::vector<core::Voice>>::ok(std::move(*voices));
}

common::Result<std::vector<core::Voice>>
VoiceService::list_voices(const VoiceFilter& filter)
{
    // First attempt.
    auto result = fetch_and_parse();

    // On HTTP 403: adjust clock skew and retry once.
    // Reference: voices.py list_voices() try/except ClientResponseError(status=403)
    //   → DRM.handle_client_response_error(e) → __list_voices() again
    if (!result &&
        result.error().code() == common::ErrorCode::service_error &&
        result.error().context() == "403")
    {
        tokens_.adjust_clock_skew(300.0);
        result = fetch_and_parse();
    }

    if (!result)
        return result;

    // Apply client-side filter (all conditions ANDed).
    // Wire order is preserved — sorting for display is the caller's concern.
    if (filter.locale || filter.gender || filter.short_name) {
        auto& v = *result;
        v.erase(std::remove_if(v.begin(), v.end(),
            [&](const core::Voice& voice) {
                if (filter.locale     && voice.locale      != *filter.locale)     return true;
                if (filter.gender     && voice.gender      != *filter.gender)     return true;
                if (filter.short_name && voice.short_name  != *filter.short_name) return true;
                return false;
            }),
        v.end());
    }

    return result;
}

} // namespace edge_tts::communication
