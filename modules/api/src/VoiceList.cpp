#include "api/VoiceList.hpp"

#include "common/Clock.hpp"
#include "common/IdGenerator.hpp"
#include "communication/EdgeServiceConfig.hpp"
#include "communication/EdgeTokenProvider.hpp"
#include "communication/HttpClient.hpp"
#include "communication/VoiceService.hpp"
#include "serialization/VoiceJsonParser.hpp"

namespace edge_tts::api {

common::Result<std::vector<core::Voice>> list_voices(SynthesisOptions options)
{
    if (options.proxy.has_value())
        return common::Result<std::vector<core::Voice>>::fail(
            common::Error{common::ErrorCode::unsupported,
                          "proxy is not supported"});

    common::SystemClock                    clock;
    common::IdGenerator                    ids;
    const communication::EdgeServiceConfig svc_config =
        communication::default_edge_service_config();
    communication::EdgeTokenProvider       tokens{svc_config, clock};

    communication::HttpClientOptions http_opts;
    http_opts.timeout = options.http_timeout;
    communication::HttpClient http{std::move(http_opts)};

    serialization::VoiceJsonParser parser;
    communication::VoiceService    svc{svc_config, http, parser, ids, tokens};

    return svc.list_voices();
}

} // namespace edge_tts::api
