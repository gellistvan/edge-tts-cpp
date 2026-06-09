#include "communication/VoiceService.hpp"
#include "communication/EdgeRequestHeaders.hpp"
#include "communication/EdgeTokenProvider.hpp"
#include "communication/HttpDate.hpp"
#include "common/Error.hpp"
#include "communication/HttpTypes.hpp"
#include "core/Voice.hpp"

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

// Build URL with a fresh DRM token and send the HTTP GET.
common::Result<HttpResponse> VoiceService::send_request()
{
    auto token = tokens_.sec_ms_gec();
    if (!token)
        return common::Result<HttpResponse>::fail(token.error());

    // Reference: voices.py __list_voices():
    //   f"{VOICE_LIST}&Sec-MS-GEC={DRM.generate_sec_ms_gec()}&Sec-MS-GEC-Version={SEC_MS_GEC_VERSION}"
    HttpRequest req;
    req.method  = "GET";
    req.url     = config_.voices_endpoint
                  + "&Sec-MS-GEC=" + *token
                  + "&Sec-MS-GEC-Version=" + tokens_.sec_ms_gec_version();
    req.headers = build_voice_list_headers(config_, ids_);

    return http_.send(req);
}

common::Result<std::vector<core::Voice>>
VoiceService::list_voices(const VoiceFilter& filter)
{
    auto resp = send_request();
    if (!resp)
        return common::Result<std::vector<core::Voice>>::fail(resp.error());

    // On HTTP 403: compute clock skew from server Date header and retry once.
    // Reference: voices.py list_voices() try/except ClientResponseError(status=403)
    //   → DRM.handle_client_response_error(e) → __list_voices() again
    if (resp->status_code == 403) {
        auto date_it = resp->headers.find("Date");
        if (date_it != resp->headers.end()) {
            auto server_ts = parse_http_date(date_it->second);
            if (server_ts) {
                tokens_.adjust_clock_skew_from_server_timestamp(
                    static_cast<double>(*server_ts));
            } else {
                tokens_.adjust_clock_skew(300.0);
            }
        } else {
            tokens_.adjust_clock_skew(300.0);
        }
        resp = send_request();
        if (!resp)
            return common::Result<std::vector<core::Voice>>::fail(resp.error());
    }

    if (resp->status_code != 200)
        return common::Result<std::vector<core::Voice>>::fail(
            {common::ErrorCode::service_error,
             "voice list HTTP request failed",
             std::to_string(resp->status_code)});

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

    return voices;
}

} // namespace edge_tts::communication
