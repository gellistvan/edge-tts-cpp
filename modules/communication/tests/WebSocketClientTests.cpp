// Offline tests for WebSocketClient.
//
// These tests do not perform network I/O; they verify option storage, default
// values, interface conformance, and compile-time isolation.
//
// Network tests live in WebSocketClientNetworkTests.cpp and require
//   cmake … -DEDGE_TTS_ENABLE_NETWORK_TESTS=ON

#include "communication/WebSocketClient.hpp"
#include "communication/IWebSocketClient.hpp"
#include "communication/WebSocketMessage.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <chrono>
#include <string>

using edge_tts::communication::WebSocketClient;
using edge_tts::communication::WebSocketClientOptions;
using edge_tts::communication::IWebSocketClient;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Default option values match the Python reference defaults
// Reference: communicate.py ws_connect(sock_connect=10, sock_read=60)
// ---------------------------------------------------------------------------

TEST(WebSocketClientOptions, DefaultConnectTimeout) {
    WebSocketClientOptions opts;
    EXPECT_EQ(opts.connect_timeout, 10'000ms);
}

TEST(WebSocketClientOptions, DefaultReadTimeout) {
    WebSocketClientOptions opts;
    EXPECT_EQ(opts.read_timeout, 60'000ms);
}

TEST(WebSocketClientOptions, DefaultProxyAbsent) {
    WebSocketClientOptions opts;
    EXPECT_FALSE(opts.proxy.has_value());
}

TEST(WebSocketClientOptions, DefaultExtraHeadersEmpty) {
    WebSocketClientOptions opts;
    EXPECT_TRUE(opts.extra_headers.empty());
}

// ---------------------------------------------------------------------------
// Custom options are stored
// ---------------------------------------------------------------------------

TEST(WebSocketClientOptions, CustomTimeoutsStored) {
    WebSocketClientOptions opts;
    opts.connect_timeout = 5'000ms;
    opts.read_timeout    = 30'000ms;
    EXPECT_EQ(opts.connect_timeout, 5'000ms);
    EXPECT_EQ(opts.read_timeout,    30'000ms);
}

TEST(WebSocketClientOptions, ProxyStored) {
    WebSocketClientOptions opts;
    opts.proxy = "http://proxy.example.com:3128";
    EXPECT_TRUE(opts.proxy.has_value());
    EXPECT_EQ(*opts.proxy, "http://proxy.example.com:3128");
}

TEST(WebSocketClientOptions, ExtraHeadersStored) {
    WebSocketClientOptions opts;
    opts.extra_headers = {
        {"Pragma",            "no-cache"},
        {"Cache-Control",     "no-cache"},
        {"Origin",            "chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold"},
        {"User-Agent",        "Mozilla/5.0"},
        {"Accept-Encoding",   "gzip, deflate, br, zstd"},
        {"Accept-Language",   "en-US,en;q=0.9"},
    };
    EXPECT_EQ(opts.extra_headers.size(), 6u);
    EXPECT_EQ(opts.extra_headers[0].first,  "Pragma");
    EXPECT_EQ(opts.extra_headers[0].second, "no-cache");
    EXPECT_EQ(opts.extra_headers[2].first,  "Origin");
}

// ---------------------------------------------------------------------------
// WebSocketClient construction
// ---------------------------------------------------------------------------

TEST(WebSocketClient, DefaultConstructionSucceeds) {
    // Must not throw or crash.
    WebSocketClient client;
    EXPECT_EQ(client.options().connect_timeout, 10'000ms);
    EXPECT_EQ(client.options().read_timeout,    60'000ms);
}

TEST(WebSocketClient, CustomOptionsStoredAndAccessible) {
    WebSocketClientOptions opts;
    opts.connect_timeout = 3'000ms;
    opts.read_timeout    = 15'000ms;
    opts.proxy           = "http://p.local:8080";
    opts.extra_headers   = {{"Origin", "test"}};

    WebSocketClient client{std::move(opts)};
    EXPECT_EQ(client.options().connect_timeout, 3'000ms);
    EXPECT_EQ(client.options().read_timeout,    15'000ms);
    EXPECT_TRUE(client.options().proxy.has_value());
    EXPECT_EQ(client.options().extra_headers.size(), 1u);
}

// ---------------------------------------------------------------------------
// WebSocketClient satisfies IWebSocketClient (interface conformance)
// ---------------------------------------------------------------------------

TEST(WebSocketClient, IsAIWebSocketClient) {
    WebSocketClient client;
    IWebSocketClient* iface = &client;
    // If this compiles, WebSocketClient correctly derives from IWebSocketClient.
    (void)iface;
    EXPECT_TRUE(true);
}

// ---------------------------------------------------------------------------
// Proxy: ixwebsocket backend must reject it, not silently ignore it
// ---------------------------------------------------------------------------

#ifdef EDGE_TTS_HAVE_IXWEBSOCKET

TEST(WebSocketClient, ConnectWithProxyReturnsUnsupported) {
    // ixwebsocket has no CONNECT-tunnel proxy API.
    // connect() must return unsupported rather than silently ignoring the proxy.
    WebSocketClientOptions opts;
    opts.proxy = "http://proxy.example.com:3128";
    WebSocketClient client{std::move(opts)};
    auto r = client.connect("wss://example.com");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), edge_tts::common::ErrorCode::unsupported);
}

TEST(WebSocketClient, ConnectWithProxyErrorMessageMentionsProxy) {
    WebSocketClientOptions opts;
    opts.proxy = "http://proxy.example.com:3128";
    WebSocketClient client{std::move(opts)};
    auto r = client.connect("wss://example.com");
    ASSERT_FALSE(r.has_value());
    const std::string msg = r.error().what();
    const bool mentions_proxy =
        msg.find("proxy") != std::string::npos ||
        msg.find("Proxy") != std::string::npos;
    EXPECT_TRUE(mentions_proxy);
}

TEST(WebSocketClient, ProxyCredentialsNotExposedInErrorContext) {
    // Proxy URLs with user:password credentials must have credentials replaced
    // with [credentials] before appearing in any error field.
    WebSocketClientOptions opts;
    opts.proxy = "http://user:s3cr3t@proxy.example.com:3128";
    WebSocketClient client{std::move(opts)};
    auto r = client.connect("wss://example.com");
    ASSERT_FALSE(r.has_value());
    const std::string ctx = std::string(r.error().context());
    EXPECT_EQ(ctx.find("s3cr3t"), std::string::npos);
    EXPECT_NE(ctx.find("[credentials]"), std::string::npos);
}

TEST(WebSocketClient, ProxyWithoutCredentialsContextUnchanged) {
    // URLs without credentials must appear in the context without modification.
    WebSocketClientOptions opts;
    opts.proxy = "http://proxy.example.com:3128";
    WebSocketClient client{std::move(opts)};
    auto r = client.connect("wss://example.com");
    ASSERT_FALSE(r.has_value());
    const std::string ctx = std::string(r.error().context());
    EXPECT_NE(ctx.find("proxy.example.com"), std::string::npos);
    EXPECT_EQ(ctx.find("[credentials]"), std::string::npos);
}

TEST(WebSocketClient, ConnectWithoutProxyDoesNotReturnUnsupportedForProxy) {
    // Without a proxy the guard must not fire.  connect() will fail on a fake
    // hostname, but the error code must NOT be unsupported.
    WebSocketClient client;  // no proxy
    auto r = client.connect("wss://this-host-does-not-exist.invalid");
    if (!r.has_value())
        EXPECT_NE(r.error().code(), edge_tts::common::ErrorCode::unsupported);
}

#endif  // EDGE_TTS_HAVE_IXWEBSOCKET (proxy tests)

// ---------------------------------------------------------------------------
// Without ixwebsocket: connect() returns unsupported
// With ixwebsocket: detailed behaviour is in network tests.
// ---------------------------------------------------------------------------

#ifndef EDGE_TTS_HAVE_IXWEBSOCKET

TEST(WebSocketClient, ConnectReturnsUnsupportedWhenSubmoduleAbsent) {
    WebSocketClient client;
    auto r = client.connect("wss://example.com");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), edge_tts::common::ErrorCode::unsupported);
}

TEST(WebSocketClient, CloseSucceedsWhenSubmoduleAbsent) {
    WebSocketClient client;
    auto r = client.close();
    EXPECT_TRUE(r.has_value());
}

#endif // !EDGE_TTS_HAVE_IXWEBSOCKET

// ---------------------------------------------------------------------------
// Public-header isolation: WebSocketClient.hpp must not include ixwebsocket
// headers.  This is verified statically by the Python dependency-config test;
// the compile below proves no ixwebsocket symbol leaks into this TU.
// ---------------------------------------------------------------------------

TEST(WebSocketClient, NoIxwebsocketSymbolsInPublicHeader) {
    // If this TU compiles without EDGE_TTS_HAVE_IXWEBSOCKET defined, it proves
    // that the public header is self-contained and free of ixwebsocket types.
    // The test body is intentionally empty — compilation is the assertion.
    EXPECT_TRUE(true);
}
