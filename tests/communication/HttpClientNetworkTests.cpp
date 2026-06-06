// Network tests for HttpClient — opt-in only.
//
// Enable with:
//   cmake -S . -B build -DEDGE_TTS_ENABLE_NETWORK_TESTS=ON
//   cmake --build build --target edge_tts_communication_network_tests
//   ctest --test-dir build -R edge_tts_communication_network_tests
//
// Do not enable in CI unless the environment has reliable outbound TLS access to
//   https://speech.platform.bing.com/consumer/speech/synthesize/readaloud/voices/list
//
// Reference: voices.py list_voices() — GET with VOICE_HEADERS, SSL, raise_for_status.

#include "edge_tts/communication/HttpClient.hpp"
#include "edge_tts/communication/VoiceService.hpp"
#include "edge_tts/communication/EdgeServiceConfig.hpp"
#include "edge_tts/common/IdGenerator.hpp"
#include "edge_tts/serialization/VoiceJsonParser.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <string>

using edge_tts::communication::HttpClient;
using edge_tts::communication::HttpClientOptions;
using edge_tts::communication::VoiceService;
using edge_tts::communication::default_edge_service_config;
using edge_tts::serialization::VoiceJsonParser;
using edge_tts::common::IdGenerator;

static IdGenerator k_net_ids{};

// ---------------------------------------------------------------------------
// GET voices endpoint — status and body
// ---------------------------------------------------------------------------

TEST(HttpClientNetwork, VoicesEndpointReturns200) {
    HttpClient client;
    auto cfg = default_edge_service_config();

    edge_tts::communication::HttpRequest req;
    req.method = "GET";
    req.url    = cfg.voices_endpoint;
    req.headers = {
        {"User-Agent",      cfg.user_agent},
        {"Accept-Encoding", "gzip, deflate, br, zstd"},
        {"Accept-Language", "en-US,en;q=0.9"},
        {"Accept",          "*/*"},
    };

    auto r = client.send(req);
    EXPECT_TRUE(r.has_value());
    if (r.has_value())
        EXPECT_EQ(r->status_code, 200);
}

TEST(HttpClientNetwork, VoicesEndpointBodyNonEmpty) {
    HttpClient client;
    auto cfg = default_edge_service_config();

    edge_tts::communication::HttpRequest req;
    req.method = "GET";
    req.url    = cfg.voices_endpoint;
    req.headers = {
        {"User-Agent",      cfg.user_agent},
        {"Accept-Encoding", "gzip, deflate, br, zstd"},
        {"Accept-Language", "en-US,en;q=0.9"},
        {"Accept",          "*/*"},
    };

    auto r = client.send(req);
    EXPECT_TRUE(r.has_value());
    if (r.has_value())
        EXPECT_FALSE(r->body.empty());
}

// ---------------------------------------------------------------------------
// VoiceService integration — parse voices from live endpoint
// ---------------------------------------------------------------------------

TEST(HttpClientNetwork, VoiceServiceParsesNonEmptyVoiceList) {
    HttpClient       client;
    VoiceJsonParser  parser;
    auto             cfg = default_edge_service_config();
    VoiceService svc{cfg, client, parser, k_net_ids};

    auto voices = svc.list_voices();
    EXPECT_TRUE(voices.has_value());
    if (voices.has_value())
        EXPECT_FALSE(voices->empty());
}

TEST(HttpClientNetwork, VoiceServiceReturnsEnUsVoices) {
    HttpClient      client;
    VoiceJsonParser parser;
    auto            cfg = default_edge_service_config();
    VoiceService svc{cfg, client, parser, k_net_ids};

    edge_tts::communication::VoiceFilter filter;
    filter.locale = "en-US";
    auto voices = svc.list_voices(filter);
    EXPECT_TRUE(voices.has_value());
    if (voices.has_value())
        EXPECT_FALSE(voices->empty());
}

TEST(HttpClientNetwork, VoiceServiceIncludesEmmaVoice) {
    // Reference: constants.py DEFAULT_VOICE = "en-US-EmmaMultilingualNeural"
    HttpClient      client;
    VoiceJsonParser parser;
    auto            cfg = default_edge_service_config();
    VoiceService svc{cfg, client, parser, k_net_ids};

    edge_tts::communication::VoiceFilter filter;
    filter.short_name = "en-US-EmmaMultilingualNeural";
    auto voices = svc.list_voices(filter);
    EXPECT_TRUE(voices.has_value());
    if (voices.has_value())
        EXPECT_EQ(voices->size(), 1u);
}
