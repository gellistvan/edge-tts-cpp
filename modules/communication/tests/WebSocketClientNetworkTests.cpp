// Network tests for WebSocketClient — opt-in only.
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
//   wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud/edge/v1
//
// Reference: communicate.py SpeechSynthesizer.__stream() — complete per-chunk lifecycle:
//   1. ws_connect(url, headers=WSS_HEADERS)
//   2. send speech.config frame
//   3. send SSML frame
//   4. receive loop until Path:turn.end
//   5. close

#include "communication/WebSocketClient.hpp"
#include "communication/SynthesisSession.hpp"
#include "communication/EdgeProtocol.hpp"
#include "communication/EdgeServiceConfig.hpp"
#include "communication/EdgeTokenProvider.hpp"
#include "communication/ConnectionMetadata.hpp"
#include "common/Clock.hpp"
#include "common/IdGenerator.hpp"
#include "core/TtsConfig.hpp"
#include "core/Chunk.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <cstdlib>
#include <string>
#include <variant>
#include <vector>

using edge_tts::communication::WebSocketClient;
using edge_tts::communication::WebSocketClientOptions;
using edge_tts::communication::SynthesisSession;
using edge_tts::communication::EdgeProtocol;
using edge_tts::communication::EdgeTokenProvider;
using edge_tts::communication::ConnectionMetadataFactory;
using edge_tts::communication::default_edge_service_config;
using edge_tts::common::SystemClock;
using edge_tts::common::IdGenerator;
using edge_tts::core::TtsConfig;
using edge_tts::core::TtsChunk;
using edge_tts::core::AudioChunk;

// Run-time gate: returns true when EDGE_TTS_RUN_NETWORK_TESTS is set.
static bool network_enabled() {
    const char* v = std::getenv("EDGE_TTS_RUN_NETWORK_TESTS");
    return v != nullptr && v[0] != '\0';
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Builds the WebSocketClientOptions with the headers the Edge TTS service
// expects on the upgrade request.
//
// Reference: communicate.py WSS_HEADERS (constants.py)
static WebSocketClientOptions make_edge_tts_options()
{
    auto cfg = default_edge_service_config();

    WebSocketClientOptions opts;
    opts.connect_timeout = std::chrono::milliseconds{15'000};
    opts.read_timeout    = std::chrono::milliseconds{60'000};

    // Reference: constants.py WSS_HEADERS
    // The Cookie muid value uses a random hex string in the Python reference
    // (token_hex(16)).  Here we use a fixed-looking value since the service
    // does not validate its content — it is present only to resemble a browser.
    opts.extra_headers = {
        {"Pragma",          "no-cache"},
        {"Cache-Control",   "no-cache"},
        {"Origin",          cfg.origin},
        {"User-Agent",      cfg.user_agent},
        {"Accept-Encoding", "gzip, deflate, br, zstd"},
        {"Accept-Language", "en-US,en;q=0.9"},
        {"Cookie",          "muid=4d65726f736f66746565646765"},
    };
    return opts;
}

// ---------------------------------------------------------------------------
// End-to-end synthesis: SynthesisSession + WebSocketClient
// ---------------------------------------------------------------------------

TEST(WebSocketClientNetwork, ShortSynthesisReturnsNonEmptyAudio) {
    if (!network_enabled()) return;
    // One chunk → one WebSocket connection → audio frames → turn.end → close.

    auto cfg = default_edge_service_config();

    WebSocketClient client{make_edge_tts_options()};
    SystemClock     clock;
    IdGenerator     ids;

    EdgeProtocol              protocol{clock};
    EdgeTokenProvider         tokens{cfg, clock};
    ConnectionMetadataFactory meta_factory{ids};

    SynthesisSession session{client, protocol, cfg, tokens, meta_factory, clock};

    TtsConfig tts;
    tts.voice = "en-US-EmmaMultilingualNeural";

    const std::vector<std::string> chunks{"Hello."};
    auto result = session.synthesize(tts, chunks);

    EXPECT_TRUE(result.has_value());
    if (!result.has_value())
        return;

    // At least one AudioChunk must be present.
    bool has_audio = false;
    for (const auto& chunk : *result) {
        if (std::holds_alternative<AudioChunk>(chunk)) {
            const auto& audio = std::get<AudioChunk>(chunk);
            if (!audio.data.empty())
                has_audio = true;
        }
    }
    EXPECT_TRUE(has_audio);
}

TEST(WebSocketClientNetwork, ShortSynthesisResultIsNonEmpty) {
    if (!network_enabled()) return;
    auto cfg = default_edge_service_config();

    WebSocketClient client{make_edge_tts_options()};
    SystemClock     clock;
    IdGenerator     ids;

    EdgeProtocol              protocol{clock};
    EdgeTokenProvider         tokens{cfg, clock};
    ConnectionMetadataFactory meta_factory{ids};

    SynthesisSession session{client, protocol, cfg, tokens, meta_factory, clock};

    TtsConfig tts;
    tts.voice = "en-US-EmmaMultilingualNeural";

    const std::vector<std::string> chunks{"Testing."};
    auto result = session.synthesize(tts, chunks);

    EXPECT_TRUE(result.has_value());
    if (result.has_value())
        EXPECT_FALSE(result->empty());
}

TEST(WebSocketClientNetwork, SynthesisReceivesTurnEnd) {
    if (!network_enabled()) return;
    // Verifies the session correctly terminates on turn.end and returns ok
    // (turn.end is the break condition in the receive loop — if it is never
    // received the session would block until read_timeout).
    auto cfg = default_edge_service_config();

    WebSocketClient client{make_edge_tts_options()};
    SystemClock     clock;
    IdGenerator     ids;

    EdgeProtocol              protocol{clock};
    EdgeTokenProvider         tokens{cfg, clock};
    ConnectionMetadataFactory meta_factory{ids};

    SynthesisSession session{client, protocol, cfg, tokens, meta_factory, clock};

    TtsConfig tts;
    tts.voice = "en-US-EmmaMultilingualNeural";

    const std::vector<std::string> chunks{"Hi."};
    auto result = session.synthesize(tts, chunks);

    // If turn.end was NOT received the session would timeout; success here
    // confirms turn.end was observed and the loop exited cleanly.
    EXPECT_TRUE(result.has_value());
}

TEST(WebSocketClientNetwork, SynthesisWithWordBoundaryMetadata) {
    if (!network_enabled()) return;
    // The word boundary metadata path exercises the audio.metadata text frame
    // branch of the receive loop in addition to the binary audio frames.
    // Reference: communicate.py — TtsConfig with WordBoundary boundary type.
    auto cfg = default_edge_service_config();

    WebSocketClient client{make_edge_tts_options()};
    SystemClock     clock;
    IdGenerator     ids;

    EdgeProtocol              protocol{clock};
    EdgeTokenProvider         tokens{cfg, clock};
    ConnectionMetadataFactory meta_factory{ids};

    SynthesisSession session{client, protocol, cfg, tokens, meta_factory, clock};

    TtsConfig tts;
    tts.voice = "en-US-EmmaMultilingualNeural";
    // WordBoundary triggers sentenceBoundaryEnabled:false, wordBoundaryEnabled:true
    tts.boundary_type = edge_tts::core::BoundaryType::word;

    const std::vector<std::string> chunks{"Hello world."};
    auto result = session.synthesize(tts, chunks);

    EXPECT_TRUE(result.has_value());
    if (!result.has_value())
        return;

    // With word boundaries enabled the result should contain boundary chunks
    // alongside audio chunks.
    bool has_audio    = false;
    bool has_boundary = false;
    for (const auto& chunk : *result) {
        if (std::holds_alternative<AudioChunk>(chunk))
            has_audio = true;
        if (std::holds_alternative<edge_tts::core::BoundaryChunk>(chunk))
            has_boundary = true;
    }
    EXPECT_TRUE(has_audio);
    EXPECT_TRUE(has_boundary);
}
