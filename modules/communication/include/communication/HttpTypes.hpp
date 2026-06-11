#pragma once

#include <map>
#include <optional>
#include <string>

namespace edge_tts::communication {

// An outgoing HTTP request.
// Method is almost always "GET" for this service; the field is kept generic.
struct HttpRequest {
    std::string                        method;   // e.g. "GET"
    std::string                        url;
    std::map<std::string, std::string> headers;
    std::optional<std::string>         body;     // absent for GET requests
};

// An HTTP response returned by IHttpClient::send().
//
// status_code matters for the Edge TTS service:
//   200  — success
//   403  — DRM token rejected; caller triggers clock-skew correction and retries
//   other non-2xx — propagated as service_error
struct HttpResponse {
    int                                status_code{200};
    std::map<std::string, std::string> headers;
    std::string                        body;
};

} // namespace edge_tts::communication
