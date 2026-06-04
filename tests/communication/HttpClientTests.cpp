#include "edge_tts/communication/HttpClient.hpp"
#include "edge_tts/communication/IHttpClient.hpp"
#include "edge_tts/common/Error.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <chrono>
#include <string>

using edge_tts::communication::HttpClient;
using edge_tts::communication::HttpClientOptions;
using edge_tts::communication::HttpRequest;
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
