#include "communication/HttpClient.hpp"
#include "communication/FakeHttpClient.hpp"
#include "communication/IHttpClient.hpp"
#include "common/Error.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <chrono>
#include <string>

using edge_tts::communication::FakeHttpClient;
using edge_tts::communication::HttpClient;
using edge_tts::communication::HttpClientOptions;
using edge_tts::communication::HttpRequest;
using edge_tts::communication::HttpResponse;
using edge_tts::common::ErrorCode;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static HttpRequest make_get(const std::string& url) {
    return {.method = "GET", .url = url, .headers = {}, .body = {}};
}

// ---------------------------------------------------------------------------
// Options: stored correctly
// ---------------------------------------------------------------------------

TEST(HttpClient, DefaultOptionsHave30sTimeout) {
    HttpClient client;
    EXPECT_EQ(client.options().timeout, std::chrono::milliseconds(30'000));
    EXPECT_FALSE(client.options().proxy.has_value());
}

TEST(HttpClient, CustomTimeoutStored) {
    HttpClientOptions opts;
    opts.timeout = std::chrono::milliseconds(5'000);
    HttpClient client{opts};
    EXPECT_EQ(client.options().timeout, std::chrono::milliseconds(5'000));
}

TEST(HttpClient, ProxyStored) {
    HttpClientOptions opts;
    opts.proxy = "http://proxy.example.com:8080";
    HttpClient client{opts};
    EXPECT_TRUE(client.options().proxy.has_value());
    EXPECT_EQ(*client.options().proxy, "http://proxy.example.com:8080");
}

TEST(HttpClient, NoProxyByDefault) {
    HttpClient client;
    EXPECT_FALSE(client.options().proxy.has_value());
}

TEST(HttpClient, TimeoutZeroStored) {
    HttpClientOptions opts;
    opts.timeout = std::chrono::milliseconds(0);
    HttpClient client{opts};
    EXPECT_EQ(client.options().timeout, std::chrono::milliseconds(0));
}

// ---------------------------------------------------------------------------
// Interface: usable as IHttpClient
// ---------------------------------------------------------------------------

TEST(HttpClient, UsableAsIHttpClientReference) {
    HttpClient client;
    // Verify the class satisfies the interface without calling the network.
    edge_tts::communication::IHttpClient& iface = client;
    (void)iface;  // just verifying the cast compiles
    EXPECT_TRUE(true);
}

// ---------------------------------------------------------------------------
// Invalid URL: transport-level error (no network required)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Proxy: ixwebsocket backend must reject it, not silently ignore it
// ---------------------------------------------------------------------------

#ifdef EDGE_TTS_HAVE_IXWEBSOCKET

TEST(HttpClient, ProxySetCausesUnsupportedError) {
    // The ixwebsocket HTTP backend has no proxy API.
    // send() must return unsupported rather than silently ignoring the proxy.
    HttpClientOptions opts;
    opts.proxy = "http://proxy.example.com:8080";
    HttpClient client{opts};
    auto r = client.send(make_get("https://example.com"));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::unsupported);
}

TEST(HttpClient, ProxyErrorMessageMentionsProxy) {
    HttpClientOptions opts;
    opts.proxy = "http://proxy.example.com:8080";
    HttpClient client{opts};
    auto r = client.send(make_get("https://example.com"));
    ASSERT_FALSE(r.has_value());
    const std::string msg = r.error().what();
    const bool mentions_proxy =
        msg.find("proxy") != std::string::npos ||
        msg.find("Proxy") != std::string::npos;
    EXPECT_TRUE(mentions_proxy);
}

TEST(HttpClient, ProxyCredentialsNotExposedInErrorContext) {
    // Proxy URLs with user:password credentials must have credentials replaced
    // with [credentials] before appearing in any error field.
    HttpClientOptions opts;
    opts.proxy = "http://user:s3cr3t@proxy.example.com:8080";
    HttpClient client{opts};
    auto r = client.send(make_get("https://example.com"));
    ASSERT_FALSE(r.has_value());
    const std::string ctx = std::string(r.error().context());
    EXPECT_EQ(ctx.find("s3cr3t"), std::string::npos);
    EXPECT_NE(ctx.find("[credentials]"), std::string::npos);
}

TEST(HttpClient, ProxyWithoutCredentialsContextUnchanged) {
    // URLs without credentials must appear in the context without modification.
    HttpClientOptions opts;
    opts.proxy = "http://proxy.example.com:8080";
    HttpClient client{opts};
    auto r = client.send(make_get("https://example.com"));
    ASSERT_FALSE(r.has_value());
    const std::string ctx = std::string(r.error().context());
    EXPECT_NE(ctx.find("proxy.example.com"), std::string::npos);
    EXPECT_EQ(ctx.find("[credentials]"), std::string::npos);
}

TEST(HttpClient, NoProxySendsRequest) {
    // Without a proxy, send() reaches ixwebsocket (which will fail on a
    // fake hostname, but the failure is transport-level, not unsupported).
    HttpClient client;  // no proxy
    auto r = client.send(make_get("https://this-host-does-not-exist.invalid"));
    // Must NOT be unsupported — that would mean the proxy guard fired incorrectly.
    if (!r.has_value())
        EXPECT_NE(r.error().code(), ErrorCode::unsupported);
}

#endif  // EDGE_TTS_HAVE_IXWEBSOCKET (proxy tests)

// ---------------------------------------------------------------------------
// IHttpClient contract: non-2xx status codes in HttpResponse (not Result::fail)
// ---------------------------------------------------------------------------

// The transport layer must return any HTTP status code as a successful
// Result<HttpResponse>.  Service-level error interpretation (403 → DRM retry,
// 5xx → service_error) is the caller's responsibility, not the transport's.
TEST(HttpClient, NonTwoxxStatusCodeReturnedInHttpResponse) {
    FakeHttpClient client;
    client.set_response({403, {}, "Forbidden"});
    auto r = client.send(make_get("https://example.com/voices"));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r->status_code, 403);
}

TEST(HttpClient, FiveHundredStatusCodeReturnedInHttpResponse) {
    FakeHttpClient client;
    client.set_response({500, {}, "Internal Server Error"});
    auto r = client.send(make_get("https://example.com/voices"));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r->status_code, 500);
}

// ---------------------------------------------------------------------------
// Invalid URL: transport-level error (no network required)
// ---------------------------------------------------------------------------

#ifdef EDGE_TTS_HAVE_IXWEBSOCKET

TEST(HttpClient, EmptyUrlReturnsError) {
    // An empty URL is malformed — ixwebsocket returns UrlMalformed immediately
    // without touching the network.
    HttpClient client;
    auto r = client.send(make_get(""));
    EXPECT_FALSE(r.has_value());
    // Should be invalid_argument (UrlMalformed) or network_error — either is
    // acceptable since it signals a transport failure without hitting a server.
    const bool is_expected_code =
        r.error().code() == ErrorCode::invalid_argument ||
        r.error().code() == ErrorCode::network_error;
    EXPECT_TRUE(is_expected_code);
}

TEST(HttpClient, MalformedUrlSchemeReturnsError) {
    // A URL with no host is clearly malformed.
    HttpClient client;
    auto r = client.send(make_get("not-a-url"));
    EXPECT_FALSE(r.has_value());
    const bool is_expected_code =
        r.error().code() == ErrorCode::invalid_argument ||
        r.error().code() == ErrorCode::network_error;
    EXPECT_TRUE(is_expected_code);
}

#else

TEST(HttpClient, StubReturnsUnsupportedWithoutIxwebsocket) {
    // When ixwebsocket is not compiled in, every send() returns unsupported.
    HttpClient client;
    auto r = client.send(make_get("https://example.com"));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::unsupported);
}

#endif  // EDGE_TTS_HAVE_IXWEBSOCKET
