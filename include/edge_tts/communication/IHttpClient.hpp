#pragma once

#include "edge_tts/common/Result.hpp"
#include "edge_tts/communication/HttpTypes.hpp"

namespace edge_tts::communication {

// Minimal HTTP transport boundary.
//
// Reference: voices.py uses aiohttp.ClientSession.get() with headers and proxy.
// Error handling: non-2xx responses are represented as Result::fail with
// ErrorCode::service_error; network-level failures use ErrorCode::network_error.
// The 403 retry-with-clock-skew logic lives in the caller (VoiceService),
// not in the transport.
//
// Proxy support and SSL details are implementation concerns; this interface
// exposes only the logical request/response boundary.
class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    [[nodiscard]] virtual common::Result<HttpResponse>
    send(const HttpRequest& request) = 0;
};

} // namespace edge_tts::communication
