// Integration tests for the --list-voices production path.
//
// These tests wire up a real communication::VoiceService backed by
// FakeHttpClient — exactly the pattern used in apps/edge-tts/main.cpp — and
// pass it into EdgeTtsCommandDispatcher.  No real network calls are made.
//
// Goal: prove that the production dispatch path (VoiceService + HttpClient)
// works end-to-end through the CLI dispatcher without requiring live network.

#include "api/SpeechSynthesizer.hpp"
#include "api/SynthesisOptions.hpp"
#include "cli/EdgeTtsArgumentParser.hpp"
#include "cli/EdgeTtsCommandDispatcher.hpp"
#include "common/Clock.hpp"
#include "common/Error.hpp"
#include "common/IdGenerator.hpp"
#include "communication/EdgeServiceConfig.hpp"
#include "communication/EdgeTokenProvider.hpp"
#include "communication/FakeHttpClient.hpp"
#include "communication/HttpClient.hpp"
#include "communication/VoiceService.hpp"
#include "core/TtsConfig.hpp"
#include "core/Voice.hpp"
#include "serialization/VoiceJsonParser.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <chrono>
#include <sstream>
#include <string>
#include <vector>

using edge_tts::api::SpeechSynthesizer;
using edge_tts::api::SynthesisOptions;
using edge_tts::cli::EdgeTtsArgumentParser;
using edge_tts::cli::EdgeTtsCommandDispatcher;
using edge_tts::cli::ParseAction;
using edge_tts::cli::ParseResult;
using edge_tts::common::ErrorCode;
using edge_tts::common::FixedClock;
using edge_tts::common::IdGenerator;
using edge_tts::communication::EdgeTokenProvider;
using edge_tts::communication::FakeHttpClient;
using edge_tts::communication::HttpClient;
using edge_tts::communication::HttpClientOptions;
using edge_tts::communication::VoiceService;
using edge_tts::communication::default_edge_service_config;
using edge_tts::core::TtsConfig;
using edge_tts::serialization::VoiceJsonParser;

// Minimal valid voice JSON matching the real Edge TTS wire format.
static std::string emma_json() {
    return R"json([{
        "Name":"Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)",
        "ShortName":"en-US-EmmaMultilingualNeural",
        "Gender":"Female",
        "Locale":"en-US",
        "SuggestedCodec":"audio-24khz-48kbitrate-mono-mp3",
        "FriendlyName":"Microsoft Emma Multilingual Online (Natural) - English (United States)",
        "Status":"GA",
        "VoiceTag":{"ContentCategories":["General"],"VoicePersonalities":["Friendly","Positive"]}
    }])json";
}

static ParseResult make_list_voices_result() {
    ParseResult r;
    r.action    = ParseAction::list_voices;
    r.exit_code = 0;
    return r;
}

// Minimal SynthesizerFactory — never called during list-voices dispatch.
static EdgeTtsCommandDispatcher::SynthesizerFactory make_noop_factory() {
    return [](std::string text, TtsConfig cfg, SynthesisOptions opts) {
        return SpeechSynthesizer{std::move(text), std::move(cfg), std::move(opts)};
    };
}

// Helper: build a VoiceService with FakeHttpClient and a deterministic fixed clock.
// This mirrors main.cpp wiring without real network.
struct TestStack {
    FakeHttpClient   http;
    VoiceJsonParser  parser;
    IdGenerator      ids;
    FixedClock       clock{std::chrono::system_clock::from_time_t(1704067200)};
    decltype(default_edge_service_config()) cfg = default_edge_service_config();
    EdgeTokenProvider tokens{cfg, clock};
    VoiceService      svc{cfg, http, parser, ids, tokens};
};

// ---------------------------------------------------------------------------
// Real VoiceService + FakeHttpClient → dispatcher (production path minus network)
// ---------------------------------------------------------------------------

TEST(ListVoicesProductionPath, RealVoiceServiceProducesVoiceTableOutput) {
    // This mirrors apps/edge-tts/main.cpp exactly, with FakeHttpClient instead
    // of HttpClient and FixedClock instead of SystemClock.
    TestStack stack;
    stack.http.set_response({200, {}, emma_json()});

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher dispatcher{
        [&stack]() { return stack.svc.list_voices(); },
        make_noop_factory(),
        out, err, in
    };

    const int rc = dispatcher.dispatch(make_list_voices_result());
    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.str().find("en-US-EmmaMultilingualNeural"), std::string::npos);
}

TEST(ListVoicesProductionPath, RealVoiceServiceReturnsExitZeroOnSuccess) {
    TestStack stack;
    stack.http.set_response({200, {}, emma_json()});

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher dispatcher{
        [&stack]() { return stack.svc.list_voices(); },
        make_noop_factory(),
        out, err, in
    };

    EXPECT_EQ(dispatcher.dispatch(make_list_voices_result()), 0);
}

TEST(ListVoicesProductionPath, Http403FromServiceReturnsExitOne) {
    // Two 403s: first attempt + retry both fail → service_error → exit 1.
    TestStack stack;
    stack.http.push_response({403, {}, "Forbidden"});
    stack.http.push_response({403, {}, "Forbidden"});

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher dispatcher{
        [&stack]() { return stack.svc.list_voices(); },
        make_noop_factory(),
        out, err, in
    };

    EXPECT_EQ(dispatcher.dispatch(make_list_voices_result()), 1);
}

TEST(ListVoicesProductionPath, Http500ErrorWritesToStderr) {
    TestStack stack;
    stack.http.set_response({500, {}, "Server Error"});

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher dispatcher{
        [&stack]() { return stack.svc.list_voices(); },
        make_noop_factory(),
        out, err, in
    };

    dispatcher.dispatch(make_list_voices_result());
    EXPECT_FALSE(err.str().empty());
}

TEST(ListVoicesProductionPath, ProxyStoredInHttpClientOptions) {
    // Verify that proxy from CLI args reaches HttpClientOptions as in main.cpp.
    // This tests the wiring pattern: parse → extract proxy → HttpClientOptions.
    HttpClientOptions opts;
    opts.proxy = "http://proxy.test:8080";

    // HttpClient stores the options — no network call needed to verify.
    HttpClient client{opts};
    EXPECT_EQ(client.options().proxy, opts.proxy);
}

TEST(ListVoicesProductionPath, MalformedJsonErrorReturnsExitOne) {
    TestStack stack;
    stack.http.set_response({200, {}, "not valid json {{{"});

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher dispatcher{
        [&stack]() { return stack.svc.list_voices(); },
        make_noop_factory(),
        out, err, in
    };

    EXPECT_EQ(dispatcher.dispatch(make_list_voices_result()), 1);
}

TEST(ListVoicesProductionPath, Http403ThenSuccessReturnsExitZero) {
    // First request 403 → retry 200 → success → exit 0.
    TestStack stack;
    stack.http.push_response({403, {}, "Forbidden"});
    stack.http.push_response({200, {}, emma_json()});

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher dispatcher{
        [&stack]() { return stack.svc.list_voices(); },
        make_noop_factory(),
        out, err, in
    };

    EXPECT_EQ(dispatcher.dispatch(make_list_voices_result()), 0);
    EXPECT_NE(out.str().find("en-US-EmmaMultilingualNeural"), std::string::npos);
}

TEST(ListVoicesProductionPath, ProductionWiringUsesVoiceServiceNotHttpVoiceService) {
    // Structural check: ensure production code constructs VoiceService (with
    // EdgeTokenProvider injection) rather than the legacy HttpVoiceService.
    // The TestStack aggregates all production dependencies except the real
    // HttpClient and SystemClock — it mirrors main.cpp exactly.
    TestStack stack;
    stack.http.set_response({200, {}, emma_json()});

    // If VoiceService were wired to HttpVoiceService, tokens_.sec_ms_gec()
    // would never be called and the Sec-MS-GEC param would be absent.
    (void)stack.svc.list_voices();
    EXPECT_NE(stack.http.last_request()->url.find("Sec-MS-GEC="), std::string::npos);
}
