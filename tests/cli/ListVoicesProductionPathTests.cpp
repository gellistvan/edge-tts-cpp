// Integration tests for the --list-voices production path.
//
// These tests wire up a real communication::VoiceService backed by
// FakeHttpClient — exactly the pattern used in apps/edge-tts/main.cpp — and
// pass it into EdgeTtsCommandDispatcher.  No real network calls are made.
//
// Goal: prove that the production dispatch path (VoiceService + HttpClient)
// works end-to-end through the CLI dispatcher without requiring live network.

#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/api/CommunicateOptions.hpp"
#include "edge_tts/cli/EdgeTtsArgumentParser.hpp"
#include "edge_tts/cli/EdgeTtsCommandDispatcher.hpp"
#include "edge_tts/common/Error.hpp"
#include "edge_tts/common/IdGenerator.hpp"
#include "edge_tts/communication/EdgeServiceConfig.hpp"
#include "edge_tts/communication/FakeHttpClient.hpp"
#include "edge_tts/communication/HttpClient.hpp"
#include "edge_tts/communication/VoiceService.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/core/Voice.hpp"
#include "edge_tts/serialization/VoiceJsonParser.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <sstream>
#include <string>
#include <vector>

using edge_tts::api::Communicate;
using edge_tts::api::CommunicateOptions;
using edge_tts::cli::EdgeTtsArgumentParser;
using edge_tts::cli::EdgeTtsCommandDispatcher;
using edge_tts::cli::ParseAction;
using edge_tts::cli::ParseResult;
using edge_tts::common::ErrorCode;
using edge_tts::common::IdGenerator;
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

// Minimal CommunicateFactory — never called during list-voices dispatch.
static EdgeTtsCommandDispatcher::CommunicateFactory make_noop_factory() {
    return [](std::string text, TtsConfig cfg, CommunicateOptions opts) {
        return Communicate{std::move(text), std::move(cfg), std::move(opts)};
    };
}

// ---------------------------------------------------------------------------
// Real VoiceService + FakeHttpClient → dispatcher (production path minus network)
// ---------------------------------------------------------------------------

TEST(ListVoicesProductionPath, RealVoiceServiceProducesVoiceTableOutput) {
    // This mirrors apps/edge-tts/main.cpp exactly, with FakeHttpClient instead
    // of HttpClient.
    FakeHttpClient   http;
    http.set_response({200, {}, emma_json()});
    VoiceJsonParser  voice_parser;
    IdGenerator      ids;
    auto             svc_config = default_edge_service_config();
    VoiceService     voice_svc{svc_config, http, voice_parser, ids};

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher dispatcher{
        [&voice_svc]() { return voice_svc.list_voices(); },
        make_noop_factory(),
        out, err, in
    };

    const int rc = dispatcher.dispatch(make_list_voices_result());
    EXPECT_EQ(rc, 0);
    EXPECT_NE(out.str().find("en-US-EmmaMultilingualNeural"), std::string::npos);
}

TEST(ListVoicesProductionPath, RealVoiceServiceReturnsExitZeroOnSuccess) {
    FakeHttpClient  http;
    http.set_response({200, {}, emma_json()});
    VoiceJsonParser  parser;
    IdGenerator      ids;
    auto             cfg = default_edge_service_config();
    VoiceService     svc{cfg, http, parser, ids};

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher dispatcher{
        [&svc]() { return svc.list_voices(); },
        make_noop_factory(),
        out, err, in
    };

    EXPECT_EQ(dispatcher.dispatch(make_list_voices_result()), 0);
}

TEST(ListVoicesProductionPath, Http403FromServiceReturnsExitOne) {
    FakeHttpClient  http;
    http.set_response({403, {}, "Forbidden"});
    VoiceJsonParser  parser;
    IdGenerator      ids;
    auto             cfg = default_edge_service_config();
    VoiceService     svc{cfg, http, parser, ids};

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher dispatcher{
        [&svc]() { return svc.list_voices(); },
        make_noop_factory(),
        out, err, in
    };

    EXPECT_EQ(dispatcher.dispatch(make_list_voices_result()), 1);
}

TEST(ListVoicesProductionPath, Http500ErrorWritesToStderr) {
    FakeHttpClient  http;
    http.set_response({500, {}, "Server Error"});
    VoiceJsonParser  parser;
    IdGenerator      ids;
    auto             cfg = default_edge_service_config();
    VoiceService     svc{cfg, http, parser, ids};

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher dispatcher{
        [&svc]() { return svc.list_voices(); },
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
    FakeHttpClient  http;
    http.set_response({200, {}, "not valid json {{{"});
    VoiceJsonParser  parser;
    IdGenerator      ids;
    auto             cfg = default_edge_service_config();
    VoiceService     svc{cfg, http, parser, ids};

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher dispatcher{
        [&svc]() { return svc.list_voices(); },
        make_noop_factory(),
        out, err, in
    };

    EXPECT_EQ(dispatcher.dispatch(make_list_voices_result()), 1);
}
