#include "edge_tts/communication/HttpClient.hpp"

#include "edge_tts/common/Error.hpp"
#include "edge_tts/communication/HttpTypes.hpp"

// ixwebsocket headers are included here, never in the public header.
// Guard ensures the file compiles even when the submodule is absent.
#ifdef EDGE_TTS_HAVE_IXWEBSOCKET
#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXSocketTLSOptions.h>
#endif

#include <algorithm>
#include <chrono>
#include <string>

namespace edge_tts::communication {

HttpClient::HttpClient(HttpClientOptions options)
    : options_(std::move(options))
{}

const HttpClientOptions& HttpClient::options() const noexcept {
    return options_;
}

#ifdef EDGE_TTS_HAVE_IXWEBSOCKET

// ---------------------------------------------------------------------------
// ixwebsocket-backed implementation
// ---------------------------------------------------------------------------

common::Result<HttpResponse> HttpClient::send(const HttpRequest& request) {
    // Proxy guard — ixwebsocket's HTTP backend has no client-side proxy API.
    // Reject early so the caller gets a clear error instead of a silent no-op.
    if (options_.proxy.has_value()) {
        return common::Result<HttpResponse>::fail(
            common::Error{common::ErrorCode::unsupported,
                          "HTTP proxy is not supported by the ixwebsocket "
                          "networking backend; remove --proxy to proceed without "
                          "a proxy",
                          *options_.proxy});
    }

    ix::HttpClient client;

    // --- TLS: use system CA bundle by default ------------------------------
    // Reference: voices.py uses ssl.create_default_context(cafile=certifi.where())
    // SocketTLSOptions with caFile="SYSTEM" delegates to the OS certificate store.
    ix::SocketTLSOptions tls_opts;
    tls_opts.caFile = "SYSTEM";
    client.setTLSOptions(tls_opts);

    // --- Build request arguments -------------------------------------------
    auto args = client.createRequest();

    // Timeout: ixwebsocket uses seconds (int); our options use milliseconds.
    // Reference: aiohttp doesn't set an explicit connect timeout by default
    // (~300s), but 30s is a sensible production default.
    const auto timeout_secs = static_cast<int>(
        std::max(std::chrono::milliseconds(1'000), options_.timeout).count() / 1000);
    args->connectTimeout  = timeout_secs;
    args->transferTimeout = timeout_secs;

    // Map request headers.
    // Reference: voices.py passes VOICE_HEADERS (User-Agent, Accept-Encoding,
    // Accept-Language, Accept) via aiohttp's headers= parameter.
    for (const auto& [key, value] : request.headers)
        args->extraHeaders[key] = value;

    // Proxy: ixwebsocket's synchronous HTTP client does not support HTTP proxy.
    // The option is preserved in options_ for documentation and future use.
    // (void) options_.proxy;  — intentionally unused until proxy is wired

    // --- Send ---------------------------------------------------------------
    const auto resp = client.get(request.url, args);
    if (!resp)
        return common::Result<HttpResponse>::fail(
            common::Error{common::ErrorCode::network_error,
                          "HTTP request failed: null response"});

    // Map ixwebsocket transport errors to our ErrorCode.
    switch (resp->errorCode) {
    case ix::HttpErrorCode::Ok:
        break;  // transport succeeded; HTTP status is in statusCode

    case ix::HttpErrorCode::Timeout:
        return common::Result<HttpResponse>::fail(
            common::Error{common::ErrorCode::timeout,
                          "HTTP request timed out",
                          request.url});

    case ix::HttpErrorCode::UrlMalformed:
    case ix::HttpErrorCode::Invalid:
        return common::Result<HttpResponse>::fail(
            common::Error{common::ErrorCode::invalid_argument,
                          "Malformed URL",
                          request.url});

    default:
        return common::Result<HttpResponse>::fail(
            common::Error{common::ErrorCode::network_error,
                          resp->errorMsg.empty() ? "HTTP transport error"
                                                 : resp->errorMsg,
                          request.url});
    }

    // Build our HttpResponse.
    HttpResponse out;
    out.status_code = resp->statusCode;
    out.body        = resp->body;
    for (const auto& [k, v] : resp->headers)
        out.headers[k] = v;

    return common::Result<HttpResponse>::ok(std::move(out));
}

#else  // EDGE_TTS_HAVE_IXWEBSOCKET not defined

// ---------------------------------------------------------------------------
// ixwebsocket not available — return unsupported error
// ---------------------------------------------------------------------------

common::Result<HttpResponse> HttpClient::send(const HttpRequest&) {
    return common::Result<HttpResponse>::fail(
        common::Error{common::ErrorCode::unsupported,
                      "HttpClient requires the ixwebsocket submodule. "
                      "Run: git submodule update --init submodules/ixwebsocket"});
}

#endif  // EDGE_TTS_HAVE_IXWEBSOCKET

} // namespace edge_tts::communication
