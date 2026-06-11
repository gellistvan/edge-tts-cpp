// Network tests for HttpClient — opt-in only.
//
// Two independent gates must both be satisfied:
//
//   # 1. Compile-time gate — build the binary:
//   cmake -S . -B build -DEDGE_TTS_ENABLE_NETWORK_TESTS=ON
//   cmake --build build --target edge_tts_communication_network_tests
//
//   # 2. Run-time gate — opt in to actual network calls:
//   EDGE_TTS_RUN_NETWORK_TESTS=1 ctest --test-dir build
//       -R edge_tts_communication_network_tests --output-on-failure
//
// Do not enable in CI unless the environment has reliable outbound TLS access to
//   https://speech.platform.bing.com/consumer/speech/synthesize/readaloud/voices/list
//

#include "communication/HttpClient.hpp"
#include "communication/VoiceService.hpp"
#include "communication/EdgeServiceConfig.hpp"
#include "communication/EdgeTokenProvider.hpp"
#include "common/Clock.hpp"
#include "common/IdGenerator.hpp"
#include "serialization/VoiceJsonParser.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <cstdlib>
#include <string>

using edge_tts::communication::HttpClient;
using edge_tts::communication::HttpClientOptions;
using edge_tts::communication::EdgeTokenProvider;
using edge_tts::communication::VoiceService;
using edge_tts::communication::default_edge_service_config;
using edge_tts::serialization::VoiceJsonParser;
using edge_tts::common::IdGenerator;
using edge_tts::common::SystemClock;

static IdGenerator   k_net_ids{};
static SystemClock   k_net_clock{};
static auto          k_net_cfg    = default_edge_service_config();
static EdgeTokenProvider k_net_tokens{k_net_cfg, k_net_clock};

// Run-time gate: returns true when EDGE_TTS_RUN_NETWORK_TESTS is set.
static bool network_enabled() {
    const char* v = std::getenv("EDGE_TTS_RUN_NETWORK_TESTS");
    return v != nullptr && v[0] != '\0';
}

// ---------------------------------------------------------------------------
// GET voices endpoint — status and body
// ---------------------------------------------------------------------------

TEST(HttpClientNetwork, VoicesEndpointReturns200) {
    if (!network_enabled()) return;
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
    if (!network_enabled()) return;
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
    if (!network_enabled()) return;
    HttpClient       client;
    VoiceJsonParser  parser;
    VoiceService svc{k_net_cfg, client, parser, k_net_ids, k_net_tokens};

    auto voices = svc.list_voices();
    EXPECT_TRUE(voices.has_value());
    if (voices.has_value())
        EXPECT_FALSE(voices->empty());
}

TEST(HttpClientNetwork, VoiceServiceReturnsEnUsVoices) {
    if (!network_enabled()) return;
    HttpClient      client;
    VoiceJsonParser parser;
    VoiceService svc{k_net_cfg, client, parser, k_net_ids, k_net_tokens};

    edge_tts::communication::VoiceFilter filter;
    filter.locale = "en-US";
    auto voices = svc.list_voices(filter);
    EXPECT_TRUE(voices.has_value());
    if (voices.has_value())
        EXPECT_FALSE(voices->empty());
}

TEST(HttpClientNetwork, VoiceServiceIncludesEmmaVoice) {
    if (!network_enabled()) return;
    // Default voice: en-US-EmmaMultilingualNeural
    HttpClient      client;
    VoiceJsonParser parser;
    VoiceService svc{k_net_cfg, client, parser, k_net_ids, k_net_tokens};

    edge_tts::communication::VoiceFilter filter;
    filter.short_name = "en-US-EmmaMultilingualNeural";
    auto voices = svc.list_voices(filter);
    EXPECT_TRUE(voices.has_value());
    if (voices.has_value())
        EXPECT_EQ(voices->size(), 1u);
}
