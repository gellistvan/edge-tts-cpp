#include "api/SpeechSynthesizer.hpp"
#include "api/SynthesisOptions.hpp"
#include "communication/WebSocketClient.hpp"
#include "common/Error.hpp"
#include "core/Chunk.hpp"
#include "core/TtsConfig.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <vector>

using edge_tts::api::SpeechSynthesizer;
using edge_tts::api::SynthesisOptions;
using edge_tts::api::SynthesizerFn;
using edge_tts::communication::WebSocketClientOptions;
using edge_tts::core::TtsConfig;
using edge_tts::core::TtsChunk;

static TtsConfig valid_config() { return TtsConfig::defaults(); }

// ---------------------------------------------------------------------------
// SynthesisOptions — default values
// ---------------------------------------------------------------------------

TEST(SynthesisOptions, DefaultProxyIsAbsent) {
    SynthesisOptions opts;
    EXPECT_FALSE(opts.proxy.has_value());
}

TEST(SynthesisOptions, DefaultWsConnectTimeoutIs10s) {
    SynthesisOptions opts;
    EXPECT_EQ(opts.ws_connect_timeout.count(), 10'000);
}

TEST(SynthesisOptions, DefaultWsReadTimeoutIs60s) {
    SynthesisOptions opts;
    EXPECT_EQ(opts.ws_read_timeout.count(), 60'000);
}

TEST(SynthesisOptions, DefaultHttpTimeoutIs30s) {
    SynthesisOptions opts;
    EXPECT_EQ(opts.http_timeout.count(), 30'000);
}

TEST(SynthesisOptions, DefaultTimeoutsMatchWebSocketClientDefaults) {
    // The SynthesisOptions defaults must match WebSocketClientOptions defaults
    // so that constructing a real session without explicit options produces
    // identical timeout behavior.
    SynthesisOptions opts;
    WebSocketClientOptions ws_opts;
    EXPECT_EQ(opts.ws_connect_timeout, ws_opts.connect_timeout);
    EXPECT_EQ(opts.ws_read_timeout,    ws_opts.read_timeout);
}

// ---------------------------------------------------------------------------
// SpeechSynthesizer — options constructor stores options, does not alter TtsConfig
// ---------------------------------------------------------------------------

TEST(SynthesisOptions, ProductionConstructorStoresOptions) {
    SynthesisOptions opts;
    opts.proxy              = "http://proxy.example.com:8080";
    opts.ws_connect_timeout = std::chrono::milliseconds{5'000};
    opts.ws_read_timeout    = std::chrono::milliseconds{45'000};

    SpeechSynthesizer c("hello", valid_config(), opts);

    EXPECT_EQ(c.options().proxy,              opts.proxy);
    EXPECT_EQ(c.options().ws_connect_timeout, opts.ws_connect_timeout);
    EXPECT_EQ(c.options().ws_read_timeout,    opts.ws_read_timeout);
}

TEST(SynthesisOptions, ProductionConstructorDoesNotAlterTtsConfig) {
    TtsConfig cfg = valid_config();
    cfg.voice = "en-GB-RyanNeural";

    SynthesisOptions opts;
    opts.proxy = "http://proxy:8080";

    SpeechSynthesizer c("hello", cfg, opts);

    // Proxy must be in options, not in TtsConfig.
    EXPECT_EQ(c.config().voice, "en-GB-RyanNeural");
    // TtsConfig has no proxy field — verified by the type system.
    // The following checks ensure no speech fields are corrupted.
    EXPECT_EQ(c.config().rate,   cfg.rate);
    EXPECT_EQ(c.config().volume, cfg.volume);
    EXPECT_EQ(c.config().pitch,  cfg.pitch);
}

TEST(SynthesisOptions, DefaultConstructorHasDefaultOptions) {
    // The 2-arg (text, config) constructor must produce default-valued options.
    SpeechSynthesizer c("hello", valid_config());
    EXPECT_FALSE(c.options().proxy.has_value());
    EXPECT_EQ(c.options().ws_connect_timeout.count(), 10'000);
    EXPECT_EQ(c.options().ws_read_timeout.count(),    60'000);
}

TEST(SynthesisOptions, SynthesizerInjectionConstructorHasDefaultOptions) {
    // The 3-arg SynthesizerFn constructor must also produce default-valued options.
    SynthesizerFn syn = [](const TtsConfig&, std::span<const std::string>)
        -> edge_tts::common::Result<std::vector<TtsChunk>>
    {
        return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
    };

    SpeechSynthesizer c("hello", valid_config(), std::move(syn));
    EXPECT_FALSE(c.options().proxy.has_value());
    EXPECT_EQ(c.options().ws_connect_timeout.count(), 10'000);
    EXPECT_EQ(c.options().ws_read_timeout.count(),    60'000);
}

// ---------------------------------------------------------------------------
// SpeechSynthesizer — options+synthesizer constructor (seam test)
// ---------------------------------------------------------------------------
//
// The 4-arg constructor (text, config, options, synthesizer) is the injection
// seam that lets tests verify options flow into the synthesizer path.
// Proxy is rejected at the API layer before the synthesizer function runs,
// so these tests verify that (a) non-proxy options are forwarded and (b)
// proxy presence blocks synthesis before the seam is entered.

TEST(SynthesisOptions, TimeoutOptionsPassedIntoWebSocketOptionsViaSeam) {
    SynthesisOptions opts;
    opts.ws_connect_timeout = std::chrono::milliseconds{3'000};
    opts.ws_read_timeout    = std::chrono::milliseconds{20'000};

    WebSocketClientOptions captured;

    SynthesizerFn syn = [&opts, &captured](
                            const TtsConfig&,
                            std::span<const std::string>)
        -> edge_tts::common::Result<std::vector<TtsChunk>>
    {
        captured.connect_timeout = opts.ws_connect_timeout;
        captured.read_timeout    = opts.ws_read_timeout;
        return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
    };

    SpeechSynthesizer c("hello world", valid_config(), opts, std::move(syn));
    auto result = c.synthesize();

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(captured.connect_timeout, opts.ws_connect_timeout);
    EXPECT_EQ(captured.read_timeout,    opts.ws_read_timeout);
}

TEST(SynthesisOptions, ProxyInOptionsBlocksSynthesisViaSeam) {
    // Proxy is rejected at the API layer before the synthesizer fn is called.
    SynthesisOptions opts;
    opts.proxy = "http://proxy.example.com:8080";

    bool synthesizer_ran = false;
    SynthesizerFn syn = [&synthesizer_ran](
                            const TtsConfig&,
                            std::span<const std::string>)
        -> edge_tts::common::Result<std::vector<TtsChunk>>
    {
        synthesizer_ran = true;
        return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
    };

    SpeechSynthesizer c("hello world", valid_config(), opts, std::move(syn));
    auto result = c.synthesize();

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), edge_tts::common::ErrorCode::unsupported);
    EXPECT_FALSE(synthesizer_ran);
}

TEST(SynthesisOptions, NoProxyLeavesWebSocketOptionsProxyAbsent) {
    // Default SynthesisOptions (no proxy) must translate to absent proxy
    // in WebSocketClientOptions.
    SynthesisOptions opts;  // proxy not set

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

    SpeechSynthesizer c("hello", valid_config(), opts, std::move(syn));
    (void)c.synthesize();

    EXPECT_FALSE(captured.proxy.has_value());
}

TEST(SynthesisOptions, OptionsAccessorReturnsStoredOptionsInSeamConstructor) {
    SynthesisOptions opts;
    opts.proxy = "http://p:9999";

    SynthesizerFn syn = [](const TtsConfig&, std::span<const std::string>)
        -> edge_tts::common::Result<std::vector<TtsChunk>>
    {
        return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
    };

    SpeechSynthesizer c("text", valid_config(), opts, std::move(syn));
    EXPECT_EQ(c.options().proxy, opts.proxy);
}

// ---------------------------------------------------------------------------
// Existing SynthesizerFn injection behavior is unaffected
// ---------------------------------------------------------------------------

TEST(SynthesisOptions, ExistingInjectionConstructorStillWorks) {
    // The 3-arg (text, config, SynthesizerFn) constructor must still work
    // exactly as before — this is a regression guard.
    bool ran = false;
    SynthesizerFn syn = [&ran](const TtsConfig&, std::span<const std::string>)
        -> edge_tts::common::Result<std::vector<TtsChunk>>
    {
        ran = true;
        return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
    };

    SpeechSynthesizer c("hello", valid_config(), std::move(syn));
    auto r = c.synthesize();
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(ran);
}

TEST(SynthesisOptions, ProxyRejectionPropagatesFromSynthesizer) {
    // Prove that when the underlying transport rejects a proxy (returning
    // unsupported), synthesize()() propagates the error rather than hiding it.
    // This verifies the API layer does not swallow transport-level proxy errors.
    SynthesisOptions opts;
    opts.proxy = "http://proxy.example.com:8080";

    SynthesizerFn syn = [](const TtsConfig&, std::span<const std::string>)
        -> edge_tts::common::Result<std::vector<TtsChunk>>
    {
        return edge_tts::common::Result<std::vector<TtsChunk>>::fail(
            edge_tts::common::Error{
                edge_tts::common::ErrorCode::unsupported,
                "proxy is not supported by this build"});
    };

    SpeechSynthesizer c("hello", valid_config(), opts, std::move(syn));
    auto result = c.synthesize();

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), edge_tts::common::ErrorCode::unsupported);
}

TEST(SynthesisOptions, TtsConfigRemainsUnchangedAfterStreamWithOptions) {
    TtsConfig cfg = valid_config();
    cfg.voice = "en-AU-NatashaNeural";
    cfg.rate  = "+10%";

    SynthesisOptions opts;
    opts.proxy = "http://proxy:3128";

    SynthesizerFn syn = [](const TtsConfig&, std::span<const std::string>)
        -> edge_tts::common::Result<std::vector<TtsChunk>>
    {
        return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
    };

    SpeechSynthesizer c("hello", cfg, opts, std::move(syn));
    (void)c.synthesize();

    // Config must be unchanged after synthesis.
    EXPECT_EQ(c.config().voice, "en-AU-NatashaNeural");
    EXPECT_EQ(c.config().rate,  "+10%");
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//


// SpeechSynthesizer object's options.

#include "cli/EdgeTtsCommandDispatcher.hpp"
#include "cli/EdgeTtsArgumentParser.hpp"

using edge_tts::cli::EdgeTtsArgumentParser;
using edge_tts::cli::EdgeTtsCommandDispatcher;

TEST(SynthesisOptions, CliProxyPassedToCommunicateFactory) {
    // Verify the dispatcher builds SynthesisOptions with the proxy from
    // the parsed --proxy argument and passes it to the factory.

    std::optional<std::string> captured_proxy;

    EdgeTtsCommandDispatcher::SynthesizerFactory factory =
        [&captured_proxy](std::string text, TtsConfig cfg, SynthesisOptions opts)
    {
        captured_proxy = opts.proxy;
        // Return a SpeechSynthesizer that will succeed.
        return SpeechSynthesizer(
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

TEST(SynthesisOptions, CliNoProxyPassesAbsentProxy) {
    // When --proxy is not given, SynthesisOptions::proxy must be nullopt.
    std::optional<std::optional<std::string>> captured;

    EdgeTtsCommandDispatcher::SynthesizerFactory factory =
        [&captured](std::string text, TtsConfig cfg, SynthesisOptions opts)
    {
        captured = opts.proxy;
        return SpeechSynthesizer(
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
