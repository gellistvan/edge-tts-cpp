#pragma once

#include "common/Result.hpp"
#include "communication/HttpTypes.hpp"

namespace edge_tts::communication {

// Minimal HTTP transport boundary.
//
// Error contract:
//   - Transport-level failures (network unreachable, TLS error, timeout,
//     unsupported proxy) → Result::fail with the appropriate ErrorCode.
//   - All HTTP responses, including non-2xx status codes, are returned as a
//     successful Result<HttpResponse>.  Service-level interpretation (e.g.
//     mapping 403 to a DRM retry or 5xx to service_error) is the caller's
//     responsibility.
//
// Reference: voices.py — aiohttp.ClientSession.get() returns a response object
// for any HTTP status; callers check e.status for 403 retry logic.
class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    [[nodiscard]] virtual common::Result<HttpResponse>
    send(const HttpRequest& request) = 0;
};

} // namespace edge_tts::communication
