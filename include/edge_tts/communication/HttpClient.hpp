#pragma once

#include "edge_tts/communication/IHttpClient.hpp"

#include <chrono>
#include <optional>
#include <string>

namespace edge_tts::communication {

// Options for HttpClient.
//
// Reference: voices.py list_voices() / aiohttp.ClientSession.get() parameters:
//   proxy   — optional HTTP/HTTPS proxy URL forwarded verbatim (e.g. "http://proxy:8080")
//   timeout — connect + transfer timeout; aiohttp default ~300s, we default 30s
struct HttpClientOptions {
    std::optional<std::string>  proxy;
    std::chrono::milliseconds   timeout{30'000};
};

// Concrete IHttpClient implementation backed by ixwebsocket's HTTP client.
//
// This class is only compiled when the ixwebsocket submodule is initialized and
// the 'ixwebsocket' CMake target exists.  When the submodule is absent, send()
// returns ErrorCode::unsupported so callers can fall back to FakeHttpClient in
// tests.
//
// Public header guarantee: no ixwebsocket types appear here.  All ixwebsocket
// includes live exclusively in src/communication/HttpClient.cpp.
//
// Error mapping (ixwebsocket → ErrorCode):
//   HttpErrorCode::UrlMalformed, Invalid → invalid_argument
//   HttpErrorCode::Timeout               → timeout
//   HttpErrorCode::Ok (transport-level)  → success; HTTP status in HttpResponse
//   all other transport failures         → network_error
//
// Note on proxy support:
//   ixwebsocket's synchronous HTTP client has no per-request proxy API.
//   If HttpClientOptions::proxy is set, send() returns ErrorCode::unsupported
//   before touching the network.  Pass an absent proxy to proceed without one.
class HttpClient final : public IHttpClient {
public:
    explicit HttpClient(HttpClientOptions options = {});

    // Access stored options (useful in tests).
    [[nodiscard]] const HttpClientOptions& options() const noexcept;

    // Send the HTTP request and return the response.
    // Returns Result::fail for transport-level failures only; non-200 status
    // codes are returned inside the successful HttpResponse so callers can
    // inspect and act on them (e.g. VoiceService handles 403 specially).
    [[nodiscard]] common::Result<HttpResponse>
    send(const HttpRequest& request) override;

private:
    HttpClientOptions options_;
};

} // namespace edge_tts::communication
