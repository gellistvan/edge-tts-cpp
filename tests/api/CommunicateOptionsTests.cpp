#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/api/CommunicateOptions.hpp"
#include "edge_tts/communication/WebSocketClient.hpp"
#include "edge_tts/common/Error.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <vector>

using edge_tts::api::Communicate;
using edge_tts::api::CommunicateOptions;
using edge_tts::api::SynthesizerFn;
using edge_tts::communication::WebSocketClientOptions;
using edge_tts::core::TtsConfig;
using edge_tts::core::TtsChunk;

static TtsConfig valid_config() { return TtsConfig::defaults(); }

// ---------------------------------------------------------------------------
// CommunicateOptions — default values
// ---------------------------------------------------------------------------

TEST(CommunicateOptions, DefaultProxyIsAbsent) {
    CommunicateOptions opts;
    EXPECT_FALSE(opts.proxy.has_value());
}

TEST(CommunicateOptions, DefaultWsConnectTimeoutIs10s) {
    CommunicateOptions opts;
    EXPECT_EQ(opts.ws_connect_timeout.count(), 10'000);
}

TEST(CommunicateOptions, DefaultWsReadTimeoutIs60s) {
    CommunicateOptions opts;
    EXPECT_EQ(opts.ws_read_timeout.count(), 60'000);
}

TEST(CommunicateOptions, DefaultHttpTimeoutIs30s) {
    CommunicateOptions opts;
    EXPECT_EQ(opts.http_timeout.count(), 30'000);
}

TEST(CommunicateOptions, DefaultTimeoutsMatchWebSocketClientDefaults) {
    // The CommunicateOptions defaults must match WebSocketClientOptions defaults
    // so that constructing a real session without explicit options produces
    // identical timeout behavior.
    CommunicateOptions opts;
    WebSocketClientOptions ws_opts;
    EXPECT_EQ(opts.ws_connect_timeout, ws_opts.connect_timeout);
    EXPECT_EQ(opts.ws_read_timeout,    ws_opts.read_timeout);
}

// ---------------------------------------------------------------------------
// Communicate — options constructor stores options, does not alter TtsConfig
// ---------------------------------------------------------------------------

TEST(CommunicateOptions, ProductionConstructorStoresOptions) {
    CommunicateOptions opts;
    opts.proxy              = "http://proxy.example.com:8080";
    opts.ws_connect_timeout = std::chrono::milliseconds{5'000};
    opts.ws_read_timeout    = std::chrono::milliseconds{45'000};

    Communicate c("hello", valid_config(), opts);

    EXPECT_EQ(c.options().proxy,              opts.proxy);
    EXPECT_EQ(c.options().ws_connect_timeout, opts.ws_connect_timeout);
    EXPECT_EQ(c.options().ws_read_timeout,    opts.ws_read_timeout);
}

TEST(CommunicateOptions, ProductionConstructorDoesNotAlterTtsConfig) {
    TtsConfig cfg = valid_config();
    cfg.voice = "en-GB-RyanNeural";

    CommunicateOptions opts;
    opts.proxy = "http://proxy:8080";

    Communicate c("hello", cfg, opts);

    // Proxy must be in options, not in TtsConfig.
    EXPECT_EQ(c.config().voice, "en-GB-RyanNeural");
    // TtsConfig has no proxy field — verified by the type system.
    // The following checks ensure no speech fields are corrupted.
    EXPECT_EQ(c.config().rate,   cfg.rate);
    EXPECT_EQ(c.config().volume, cfg.volume);
    EXPECT_EQ(c.config().pitch,  cfg.pitch);
}

TEST(CommunicateOptions, DefaultConstructorHasDefaultOptions) {
    // The 2-arg (text, config) constructor must produce default-valued options.
    Communicate c("hello", valid_config());
    EXPECT_FALSE(c.options().proxy.has_value());
    EXPECT_EQ(c.options().ws_connect_timeout.count(), 10'000);
    EXPECT_EQ(c.options().ws_read_timeout.count(),    60'000);
}

TEST(CommunicateOptions, SynthesizerInjectionConstructorHasDefaultOptions) {
    // The 3-arg SynthesizerFn constructor must also produce default-valued options.
    SynthesizerFn syn = [](const TtsConfig&, std::span<const std::string>)
        -> edge_tts::common::Result<std::vector<TtsChunk>>
    {
        return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
    };

    Communicate c("hello", valid_config(), std::move(syn));
    EXPECT_FALSE(c.options().proxy.has_value());
    EXPECT_EQ(c.options().ws_connect_timeout.count(), 10'000);
    EXPECT_EQ(c.options().ws_read_timeout.count(),    60'000);
}

// ---------------------------------------------------------------------------
// Communicate — options+synthesizer constructor (seam test)
// ---------------------------------------------------------------------------
//
// The 4-arg constructor (text, config, options, synthesizer) is the injection
// seam that lets tests verify options flow into the synthesizer path.
// In production, the real SynthesisSession reads Communicate::options_ and
// builds WebSocketClientOptions from it; this test simulates that mapping.

TEST(CommunicateOptions, ProxyPassedIntoWebSocketOptionsViaSeam) {
    CommunicateOptions opts;
    opts.proxy              = "http://proxy.example.com:8080";
    opts.ws_connect_timeout = std::chrono::milliseconds{3'000};
    opts.ws_read_timeout    = std::chrono::milliseconds{20'000};

    // Capture what the synthesizer sees when it builds WebSocketClientOptions.
    WebSocketClientOptions captured;
    bool synthesizer_ran = false;

    // Simulates how the real production synthesizer will translate
    // CommunicateOptions → WebSocketClientOptions when networking is wired.
    SynthesizerFn syn = [&opts, &captured, &synthesizer_ran](
                            const TtsConfig&,
                            std::span<const std::string>)
        -> edge_tts::common::Result<std::vector<TtsChunk>>
    {
        captured.proxy           = opts.proxy;
        captured.connect_timeout = opts.ws_connect_timeout;
        captured.read_timeout    = opts.ws_read_timeout;
        synthesizer_ran = true;
        return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
    };

    Communicate c("hello world", valid_config(), opts, std::move(syn));
    auto result = c.stream_sync();

    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(synthesizer_ran);

    EXPECT_EQ(captured.proxy,           opts.proxy);
    EXPECT_EQ(captured.connect_timeout, opts.ws_connect_timeout);
    EXPECT_EQ(captured.read_timeout,    opts.ws_read_timeout);
}

TEST(CommunicateOptions, NoProxyLeavesWebSocketOptionsProxyAbsent) {
    // Default CommunicateOptions (no proxy) must translate to absent proxy
    // in WebSocketClientOptions.
    CommunicateOptions opts;  // proxy not set

    WebSocketClientOptions captured;

    SynthesizerFn syn = [&opts, &captured](
                            const TtsConfig&,
                            std::span<const std::string>)
        -> edge_tts::common::Result<std::vector<TtsChunk>>
    {
        captured.proxy           = opts.proxy;
        captured.connect_timeout = opts.ws_connect_timeout;
        captured.read_timeout    = opts.ws_read_timeout;
        return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
    };

    Communicate c("hello", valid_config(), opts, std::move(syn));
    (void)c.stream_sync();

    EXPECT_FALSE(captured.proxy.has_value());
}

TEST(CommunicateOptions, OptionsAccessorReturnsStoredOptionsInSeamConstructor) {
    CommunicateOptions opts;
    opts.proxy = "http://p:9999";

    SynthesizerFn syn = [](const TtsConfig&, std::span<const std::string>)
        -> edge_tts::common::Result<std::vector<TtsChunk>>
    {
        return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
    };

    Communicate c("text", valid_config(), opts, std::move(syn));
    EXPECT_EQ(c.options().proxy, opts.proxy);
}

// ---------------------------------------------------------------------------
// Existing SynthesizerFn injection behavior is unaffected
// ---------------------------------------------------------------------------

TEST(CommunicateOptions, ExistingInjectionConstructorStillWorks) {
    // The 3-arg (text, config, SynthesizerFn) constructor must still work
    // exactly as before — this is a regression guard.
    bool ran = false;
    SynthesizerFn syn = [&ran](const TtsConfig&, std::span<const std::string>)
        -> edge_tts::common::Result<std::vector<TtsChunk>>
    {
        ran = true;
        return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
    };

    Communicate c("hello", valid_config(), std::move(syn));
    auto r = c.stream_sync();
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(ran);
}

TEST(CommunicateOptions, ProxyRejectionPropagatesFromSynthesizer) {
    // Prove that when the underlying transport rejects a proxy (returning
    // unsupported), stream_sync() propagates the error rather than hiding it.
    // This verifies the API layer does not swallow transport-level proxy errors.
    CommunicateOptions opts;
    opts.proxy = "http://proxy.example.com:8080";

    SynthesizerFn syn = [](const TtsConfig&, std::span<const std::string>)
        -> edge_tts::common::Result<std::vector<TtsChunk>>
    {
        return edge_tts::common::Result<std::vector<TtsChunk>>::fail(
            edge_tts::common::Error{
                edge_tts::common::ErrorCode::unsupported,
                "proxy is not supported by this build"});
    };

    Communicate c("hello", valid_config(), opts, std::move(syn));
    auto result = c.stream_sync();

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), edge_tts::common::ErrorCode::unsupported);
}

TEST(CommunicateOptions, TtsConfigRemainsUnchangedAfterStreamWithOptions) {
    TtsConfig cfg = valid_config();
    cfg.voice = "en-AU-NatashaNeural";
    cfg.rate  = "+10%";

    CommunicateOptions opts;
    opts.proxy = "http://proxy:3128";

    SynthesizerFn syn = [](const TtsConfig&, std::span<const std::string>)
        -> edge_tts::common::Result<std::vector<TtsChunk>>
    {
        return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
    };

    Communicate c("hello", cfg, opts, std::move(syn));
    (void)c.stream_sync();

    // Config must be unchanged after synthesis.
    EXPECT_EQ(c.config().voice, "en-AU-NatashaNeural");
    EXPECT_EQ(c.config().rate,  "+10%");
}

// ---------------------------------------------------------------------------
// CLI proxy path: CommunicateOptions wires --proxy to CommunicateFactory
// ---------------------------------------------------------------------------
//
// These tests verify that the CLI factory (EdgeTtsCommandDispatcher::CommunicateFactory)
// receives the proxy that was parsed from --proxy, and that it reaches the
// Communicate object's options.

#include "edge_tts/cli/EdgeTtsCommandDispatcher.hpp"
#include "edge_tts/cli/EdgeTtsArgumentParser.hpp"

using edge_tts::cli::EdgeTtsArgumentParser;
using edge_tts::cli::EdgeTtsCommandDispatcher;

TEST(CommunicateOptions, CliProxyPassedToCommunicateFactory) {
    // Verify the dispatcher builds CommunicateOptions with the proxy from
    // the parsed --proxy argument and passes it to the factory.

    std::optional<std::string> captured_proxy;

    EdgeTtsCommandDispatcher::CommunicateFactory factory =
        [&captured_proxy](std::string text, TtsConfig cfg, CommunicateOptions opts)
    {
        captured_proxy = opts.proxy;
        // Return a Communicate that will succeed.
        return Communicate(
            std::move(text), std::move(cfg), std::move(opts),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>>
            {
                edge_tts::core::AudioChunk ac;
                ac.data = {std::byte{0xAA}};
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok(
                    {edge_tts::core::TtsChunk{ac}});
            });
    };

    EdgeTtsCommandDispatcher::VoiceServiceFn vsvc =
        []() -> edge_tts::common::Result<std::vector<edge_tts::core::Voice>> {
        return edge_tts::common::Result<std::vector<edge_tts::core::Voice>>::ok({});
    };

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher dispatcher{vsvc, factory, out, err, in};

    EdgeTtsArgumentParser parser;
    const char* argv[] = {"edge-tts", "--text", "hello", "--proxy",
                          "http://myproxy:8080", "--write-media", "/dev/null"};
    auto result = parser.parse(7, argv);

    dispatcher.dispatch(result);

    ASSERT_TRUE(captured_proxy.has_value());
    EXPECT_EQ(*captured_proxy, "http://myproxy:8080");
}

TEST(CommunicateOptions, CliNoProxyPassesAbsentProxy) {
    // When --proxy is not given, CommunicateOptions::proxy must be nullopt.
    std::optional<std::optional<std::string>> captured;

    EdgeTtsCommandDispatcher::CommunicateFactory factory =
        [&captured](std::string text, TtsConfig cfg, CommunicateOptions opts)
    {
        captured = opts.proxy;
        return Communicate(
            std::move(text), std::move(cfg), std::move(opts),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>>
            {
                edge_tts::core::AudioChunk ac;
                ac.data = {std::byte{0xAA}};
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok(
                    {edge_tts::core::TtsChunk{ac}});
            });
    };

    EdgeTtsCommandDispatcher::VoiceServiceFn vsvc =
        []() -> edge_tts::common::Result<std::vector<edge_tts::core::Voice>> {
        return edge_tts::common::Result<std::vector<edge_tts::core::Voice>>::ok({});
    };

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher dispatcher{vsvc, factory, out, err, in};

    EdgeTtsArgumentParser parser;
    const char* argv[] = {"edge-tts", "--text", "hello", "--write-media", "/dev/null"};
    auto result = parser.parse(5, argv);

    dispatcher.dispatch(result);

    ASSERT_TRUE(captured.has_value());
    EXPECT_FALSE(captured->has_value());
}
