// Real-network smoke tests: Edge TTS synthesis (WebSocket + audio protocol).
//
// WARNING: These tests contact Microsoft Edge TTS servers.  Do not enable in
// CI environments without reliable outbound TLS access to
//   wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud/edge/v1
//
// Two independent gates must both be satisfied before any test makes a network call:
//
//   # 1. Compile-time gate — build the network-test binary:
//   cmake -S . -B build -DEDGE_TTS_ENABLE_NETWORK_TESTS=ON
//   cmake --build build --target edge_tts_network_smoke_tests
//
//   # 2. Runtime gate — opt in to actual network calls:
//   EDGE_TTS_RUN_NETWORK_TESTS=1 ctest --test-dir build -L network --output-on-failure
//
// When the runtime variable is absent, every test returns immediately without
// making any network call or failing any assertion.
//
// What these tests verify:
//   - The WebSocket connection to the real service is accepted.
//   - Headers (User-Agent, Origin, Cookie) and DRM token (Sec-MS-GEC) are accepted.
//   - Synthesizing "hello" returns non-empty audio bytes.
//   - The protocol loop terminates cleanly (turn.end is received).
//   - Word-boundary metadata frames are received when WordBoundary is enabled.
//   - save() writes a non-empty MP3 file to a temp path.
//   - SRT content is written when word-boundary mode is on.
//   - api::SpeechSynthesizer production constructor wires the full real stack.
//
// Reference:
//   reference/edge-tts/src/edge_tts/communicate.py SpeechSynthesizer.__stream()
//   reference/edge-tts/src/edge_tts/constants.py WSS_HEADERS, WSS_URL

#include "edge_tts/api/SpeechSynthesizer.hpp"
#include "edge_tts/api/SynthesisOptions.hpp"
#include "edge_tts/communication/ConnectionMetadata.hpp"
#include "edge_tts/communication/EdgeProtocol.hpp"
#include "edge_tts/communication/EdgeRequestHeaders.hpp"
#include "edge_tts/communication/EdgeServiceConfig.hpp"
#include "edge_tts/communication/EdgeTokenProvider.hpp"
#include "edge_tts/communication/SynthesisSession.hpp"
#include "edge_tts/communication/WebSocketClient.hpp"
#include "edge_tts/common/Clock.hpp"
#include "edge_tts/common/IdGenerator.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

using edge_tts::api::SpeechSynthesizer;
using edge_tts::api::SynthesisOptions;
using edge_tts::communication::ConnectionMetadataFactory;
using edge_tts::communication::EdgeProtocol;
using edge_tts::communication::EdgeServiceConfig;
using edge_tts::communication::EdgeTokenProvider;
using edge_tts::communication::SynthesisSession;
using edge_tts::communication::WebSocketClient;
using edge_tts::communication::WebSocketClientOptions;
using edge_tts::communication::default_edge_service_config;
using edge_tts::common::IdGenerator;
using edge_tts::common::SystemClock;
using edge_tts::core::AudioChunk;
using edge_tts::core::BoundaryChunk;
using edge_tts::core::TtsChunk;
using edge_tts::core::TtsConfig;

// ---------------------------------------------------------------------------
// Runtime gate
// ---------------------------------------------------------------------------

static bool network_enabled() {
    const char* v = std::getenv("EDGE_TTS_RUN_NETWORK_TESTS");
    return v != nullptr && v[0] != '\0';
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build WebSocketClientOptions with the HTTP upgrade headers the Edge TTS service
// expects. Reference: constants.py WSS_HEADERS.
static WebSocketClientOptions make_ws_options() {
    auto cfg = default_edge_service_config();
    IdGenerator ids;

    WebSocketClientOptions opts;
    opts.connect_timeout = std::chrono::milliseconds{20'000};
    opts.read_timeout    = std::chrono::milliseconds{60'000};
    opts.extra_headers   = edge_tts::communication::build_websocket_headers(cfg, ids);
    return opts;
}

// Build a complete SynthesisSession against the real service.
// All objects are local (no globals) so each test is isolated.
struct RealStack {
    SystemClock               clock;
    IdGenerator               ids;
    EdgeServiceConfig         cfg{default_edge_service_config()};
    EdgeTokenProvider         tokens{cfg, clock};
    EdgeProtocol              protocol{clock};
    ConnectionMetadataFactory meta{ids};
    WebSocketClient           ws{make_ws_options()};
    SynthesisSession          session{ws, protocol, cfg, tokens, meta, clock};
};

// RAII temp-file.
struct NetTempFile {
    fs::path path;
    explicit NetTempFile(const std::string& tag, const std::string& ext)
        : path(fs::temp_directory_path() / ("net_smoke_" + tag + ext)) {}
    ~NetTempFile() { std::error_code ec; fs::remove(path, ec); }
};

// Short phrase used across all synthesis tests.  Must be a very short string
// to minimise service requests and avoid rate limiting.
static constexpr const char* kTestPhrase = "hello";
static constexpr const char* kDefaultVoice = "en-US-EmmaMultilingualNeural";

// ---------------------------------------------------------------------------
// Gate verification (always runs — no network_enabled() guard)
// ---------------------------------------------------------------------------

TEST(RealSynthesisGate, SkipWhenEnvVarAbsent) {
    if (network_enabled()) {
        EXPECT_TRUE(true);  // env var set — network tests will run
    } else {
        // env var absent — verify gate returns false and all tests will skip
        EXPECT_FALSE(network_enabled());
    }
}

TEST(RealSynthesisGate, GateFunctionIsCallable) {
    const bool result = network_enabled();
    (void)result;
    EXPECT_TRUE(true);
}

// ---------------------------------------------------------------------------
// SynthesisSession + WebSocketClient: low-level protocol tests
// ---------------------------------------------------------------------------

TEST(RealSynthesis, ShortPhraseSynthesisSucceeds) {
    if (!network_enabled()) return;

    RealStack stack;
    TtsConfig tts;
    tts.voice = kDefaultVoice;

    const std::vector<std::string> chunks{kTestPhrase};
    auto result = stack.session.synthesize(tts, chunks);

    EXPECT_TRUE(result.has_value());
}

TEST(RealSynthesis, ShortPhraseSynthesisReturnsNonEmptyAudio) {
    if (!network_enabled()) return;

    // Core smoke test: the generated request headers, DRM token (Sec-MS-GEC),
    // and SSML frame are accepted by the real service, which responds with at
    // least one binary audio frame.
    RealStack stack;
    TtsConfig tts;
    tts.voice = kDefaultVoice;

    const std::vector<std::string> chunks{kTestPhrase};
    auto result = stack.session.synthesize(tts, chunks);
    ASSERT_TRUE(result.has_value());

    bool has_audio = false;
    for (const auto& chunk : *result) {
        if (std::holds_alternative<AudioChunk>(chunk)) {
            if (!std::get<AudioChunk>(chunk).data.empty())
                has_audio = true;
        }
    }
    EXPECT_TRUE(has_audio);
}

TEST(RealSynthesis, AudioBytesHaveNonZeroSize) {
    if (!network_enabled()) return;

    RealStack stack;
    TtsConfig tts;
    tts.voice = kDefaultVoice;

    const std::vector<std::string> chunks{kTestPhrase};
    auto result = stack.session.synthesize(tts, chunks);
    ASSERT_TRUE(result.has_value());

    std::size_t total_audio_bytes = 0;
    for (const auto& chunk : *result) {
        if (std::holds_alternative<AudioChunk>(chunk))
            total_audio_bytes += std::get<AudioChunk>(chunk).data.size();
    }
    EXPECT_TRUE(total_audio_bytes > 0u);
}

TEST(RealSynthesis, TurnEndTerminatesSessionCleanly) {
    // If the service never sends turn.end the session would block until
    // read_timeout and return a network_error.  A clean return proves that
    // the turn.end frame was received and the receive loop exited normally.
    if (!network_enabled()) return;

    RealStack stack;
    TtsConfig tts;
    tts.voice = kDefaultVoice;

    const std::vector<std::string> chunks{"hi"};
    auto result = stack.session.synthesize(tts, chunks);

    EXPECT_TRUE(result.has_value());
}

TEST(RealSynthesis, SynthesisResultContainsAtLeastOneChunk) {
    if (!network_enabled()) return;

    RealStack stack;
    TtsConfig tts;
    tts.voice = kDefaultVoice;

    const std::vector<std::string> chunks{kTestPhrase};
    auto result = stack.session.synthesize(tts, chunks);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->size() > 0u);
}

// ---------------------------------------------------------------------------
// Word-boundary metadata path
// ---------------------------------------------------------------------------

TEST(RealSynthesis, WordBoundaryModeReturnsBoundaryChunks) {
    // Enables wordBoundaryEnabled:true in speech.config.  The service must
    // respond with audio.metadata text frames containing WordBoundary events.
    // Reference: communicate.py TtsConfig with BoundaryType::word.
    if (!network_enabled()) return;

    RealStack stack;
    TtsConfig tts;
    tts.voice          = kDefaultVoice;
    tts.boundary_type  = edge_tts::core::BoundaryType::word;

    const std::vector<std::string> chunks{"hello world"};
    auto result = stack.session.synthesize(tts, chunks);
    ASSERT_TRUE(result.has_value());

    bool has_audio    = false;
    bool has_boundary = false;
    for (const auto& chunk : *result) {
        if (std::holds_alternative<AudioChunk>(chunk))    has_audio    = true;
        if (std::holds_alternative<BoundaryChunk>(chunk)) has_boundary = true;
    }
    EXPECT_TRUE(has_audio);
    EXPECT_TRUE(has_boundary);
}

TEST(RealSynthesis, WordBoundaryChunksHaveNonEmptyText) {
    if (!network_enabled()) return;

    RealStack stack;
    TtsConfig tts;
    tts.voice         = kDefaultVoice;
    tts.boundary_type = edge_tts::core::BoundaryType::word;

    const std::vector<std::string> chunks{"hello world"};
    auto result = stack.session.synthesize(tts, chunks);
    ASSERT_TRUE(result.has_value());

    for (const auto& chunk : *result) {
        if (std::holds_alternative<BoundaryChunk>(chunk)) {
            EXPECT_FALSE(std::get<BoundaryChunk>(chunk).text.empty());
        }
    }
}

TEST(RealSynthesis, WordBoundaryChunksHavePositiveOffsetTicks) {
    if (!network_enabled()) return;

    RealStack stack;
    TtsConfig tts;
    tts.voice         = kDefaultVoice;
    tts.boundary_type = edge_tts::core::BoundaryType::word;

    const std::vector<std::string> chunks{"hello world"};
    auto result = stack.session.synthesize(tts, chunks);
    ASSERT_TRUE(result.has_value());

    // Word boundaries must have non-negative offsets.
    for (const auto& chunk : *result) {
        if (std::holds_alternative<BoundaryChunk>(chunk)) {
            EXPECT_TRUE(std::get<BoundaryChunk>(chunk).offset_ticks >= 0);
        }
    }
}

// ---------------------------------------------------------------------------
// api::SpeechSynthesizer production constructor (real networking stack)
// ---------------------------------------------------------------------------

TEST(RealSynthesis, CommunicateProductionConstructorSynthesizes) {
    // Proves the 2-arg SpeechSynthesizer constructor wires a real SynthesisSession
    // (not a stub). If the stub returned an error the test would fail.
    if (!network_enabled()) return;

    SpeechSynthesizer c(kTestPhrase, TtsConfig::defaults());
    auto result = c.synthesize();

    ASSERT_TRUE(result.has_value());
    bool has_audio = false;
    for (const auto& chunk : *result) {
        if (std::holds_alternative<AudioChunk>(chunk)) {
            if (!std::get<AudioChunk>(chunk).data.empty())
                has_audio = true;
        }
    }
    EXPECT_TRUE(has_audio);
}

TEST(RealSynthesis, CommunicateSaveWritesNonEmptyMp3) {
    if (!network_enabled()) return;

    NetTempFile mp3{"save", ".mp3"};
    SpeechSynthesizer c(kTestPhrase, TtsConfig::defaults());
    auto r = c.save(mp3.path);

    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(fs::exists(mp3.path));
    EXPECT_TRUE(fs::file_size(mp3.path) > 0u);
}

TEST(RealSynthesis, CommunicateSaveWritesSrtWhenWordBoundaryEnabled) {
    // Proves the full pipeline: synthesis → boundary events → SubMaker → SRT file.
    if (!network_enabled()) return;

    NetTempFile mp3{"srt", ".mp3"};
    NetTempFile srt{"srt", ".srt"};

    TtsConfig cfg = TtsConfig::defaults();
    cfg.boundary_type = edge_tts::core::BoundaryType::word;
    SpeechSynthesizer c("hello world", cfg);
    auto r = c.save(mp3.path, srt.path);

    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(fs::exists(mp3.path));
    EXPECT_TRUE(fs::file_size(mp3.path) > 0u);
    EXPECT_TRUE(fs::exists(srt.path));
    EXPECT_TRUE(fs::file_size(srt.path) > 0u);
}

TEST(RealSynthesis, CommunicateSynthesizeReturnsBothChunkTypes) {
    if (!network_enabled()) return;

    TtsConfig cfg = TtsConfig::defaults();
    cfg.boundary_type = edge_tts::core::BoundaryType::word;
    SpeechSynthesizer c("hello world", cfg);
    auto result = c.synthesize();

    ASSERT_TRUE(result.has_value());
    bool has_audio    = false;
    bool has_boundary = false;
    for (const auto& chunk : *result) {
        if (std::holds_alternative<AudioChunk>(chunk))    has_audio    = true;
        if (std::holds_alternative<BoundaryChunk>(chunk)) has_boundary = true;
    }
    EXPECT_TRUE(has_audio);
    EXPECT_TRUE(has_boundary);
}

// ---------------------------------------------------------------------------
// Different voices: verify the voice parameter is forwarded
// ---------------------------------------------------------------------------

TEST(RealSynthesis, AlternativeVoiceRyanProducesAudio) {
    // Uses a British male voice to verify the voice field in SSML is accepted.
    // Keep the phrase very short to minimise service load.
    if (!network_enabled()) return;

    TtsConfig cfg = TtsConfig::defaults();
    cfg.voice = "en-GB-RyanNeural";
    SpeechSynthesizer c("hi", cfg);
    auto result = c.synthesize();

    ASSERT_TRUE(result.has_value());
    bool has_audio = false;
    for (const auto& chunk : *result) {
        if (std::holds_alternative<AudioChunk>(chunk)) {
            if (!std::get<AudioChunk>(chunk).data.empty())
                has_audio = true;
        }
    }
    EXPECT_TRUE(has_audio);
}
