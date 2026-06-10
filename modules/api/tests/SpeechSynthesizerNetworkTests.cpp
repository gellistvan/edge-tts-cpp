// Real-network integration tests for api::SpeechSynthesizer.
//
// These tests call the live Microsoft Edge TTS service.  They are disabled by
// default and must NOT run in standard CI.
//
// How to enable:
//
//   # 1. Build with the network-test target enabled (compile-time gate):
//   cmake -S . -B build -DEDGE_TTS_ENABLE_NETWORK_TESTS=ON
//   cmake --build build
//
//   # 2. Set the run-time gate to opt in to actual network calls:
//   EDGE_TTS_RUN_NETWORK_TESTS=1 ctest --test-dir build
//       -R edge_tts_api_network_tests --output-on-failure
//
// Both gates must be satisfied for the tests to execute.  The compile-time gate
// (EDGE_TTS_ENABLE_NETWORK_TESTS) prevents the binary from being built in
// environments that should never touch the network.  The run-time gate
// (EDGE_TTS_RUN_NETWORK_TESTS) lets CI scripts control execution at the job
// level without recompiling.
//
// If the service is reachable but the test binary was built without
// EDGE_TTS_ENABLE_NETWORK_TESTS=ON, these tests simply do not exist.
//
// TLS requirement: ixwebsocket must be built with TLS support (USE_TLS=ON,
// the default).  Requires a system OpenSSL installation.

#include "api/SpeechSynthesizer.hpp"
#include "api/SynthesisOptions.hpp"
#include "core/Chunk.hpp"
#include "core/TtsConfig.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

using edge_tts::api::SpeechSynthesizer;
using edge_tts::api::SynthesisOptions;
using edge_tts::core::AudioChunk;
using edge_tts::core::BoundaryChunk;
using edge_tts::core::TtsConfig;

// ---------------------------------------------------------------------------
// Run-time gate
// ---------------------------------------------------------------------------

// Returns true when EDGE_TTS_RUN_NETWORK_TESTS is set to a non-empty value.
// Tests call this at the top of their body and return immediately when false,
// so the binary still links and the CTest entry shows a pass (no assertion
// fired) rather than a skip — compatible with minigtest which has no skip
// mechanism.
static bool network_enabled() {
    const char* v = std::getenv("EDGE_TTS_RUN_NETWORK_TESTS");
    return v != nullptr && v[0] != '\0';
}

// RAII temp-file cleanup.
struct TempFileN {
    fs::path path;
    explicit TempFileN(const std::string& tag, const std::string& ext)
        : path(fs::temp_directory_path() / ("e2e_net_" + tag + ext)) {}
    ~TempFileN() { std::error_code ec; fs::remove(path, ec); }
};

// ---------------------------------------------------------------------------
// Smoke test: synthesize()() returns non-empty audio
// ---------------------------------------------------------------------------

TEST(CommunicateNetwork, SynthesizeReturnsNonEmptyAudio) {
    if (!network_enabled()) return;

    SpeechSynthesizer c("Hi.", TtsConfig::defaults());
    auto result = c.synthesize();

    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->empty());

    bool has_audio = false;
    for (const auto& chunk : *result) {
        if (std::holds_alternative<AudioChunk>(chunk)) {
            if (!std::get<AudioChunk>(chunk).data.empty())
                has_audio = true;
        }
    }
    EXPECT_TRUE(has_audio);
}

// ---------------------------------------------------------------------------
// save() writes a non-empty MP3 file
// ---------------------------------------------------------------------------

TEST(CommunicateNetwork, SaveWritesMp3File) {
    if (!network_enabled()) return;

    TempFileN mp3{"save", ".mp3"};
    SpeechSynthesizer c("Test.", TtsConfig::defaults());
    ASSERT_TRUE(c.save(mp3.path).has_value());

    EXPECT_TRUE(fs::exists(mp3.path));
    EXPECT_TRUE(fs::file_size(mp3.path) > 0u);
}

// ---------------------------------------------------------------------------
// save() with word-boundary config writes a non-empty SRT file
// ---------------------------------------------------------------------------

TEST(CommunicateNetwork, SaveWithWordBoundaryWritesSrtFile) {
    if (!network_enabled()) return;

    TempFileN mp3{"srt_net", ".mp3"};
    TempFileN srt{"srt_net", ".srt"};

    TtsConfig cfg = TtsConfig::defaults();
    cfg.boundary_type = edge_tts::core::BoundaryType::word;
    SpeechSynthesizer c("Hello world.", cfg);
    ASSERT_TRUE(c.save(mp3.path, srt.path).has_value());

    EXPECT_TRUE(fs::exists(mp3.path));
    EXPECT_TRUE(fs::file_size(mp3.path) > 0u);
    EXPECT_TRUE(fs::exists(srt.path));
    EXPECT_TRUE(fs::file_size(srt.path) > 0u);
}

// ---------------------------------------------------------------------------
// synthesize()() with word-boundary config returns boundary chunks
// ---------------------------------------------------------------------------

TEST(CommunicateNetwork, SynthesizeWithWordBoundaryReturnsBoundaryChunks) {
    if (!network_enabled()) return;

    TtsConfig cfg = TtsConfig::defaults();
    cfg.boundary_type = edge_tts::core::BoundaryType::word;
    SpeechSynthesizer c("Hello world.", cfg);
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
// Proxy option: rejected at the API layer before any network call
// ---------------------------------------------------------------------------

TEST(CommunicateNetwork, ProxyYieldsUnsupported) {
    if (!network_enabled()) return;

    SynthesisOptions opts;
    opts.proxy = "http://127.0.0.1:1";
    SpeechSynthesizer c("Hello.", TtsConfig::defaults(), opts);
    auto result = c.synthesize();

    // Proxy is rejected at the API layer — must return unsupported without
    // making any network call.
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), edge_tts::common::ErrorCode::unsupported);
}

// ---------------------------------------------------------------------------
// Production constructor wiring — requires real network.
//
// These tests prove that the 2-arg and 3-arg production constructors wire the
// real networking stack and can produce audio when the service is reachable.
// The offline structural equivalents live in CommunicateProductionWiringTests.cpp.
// ---------------------------------------------------------------------------

TEST(CommunicateNetwork, ProductionConstructorCanSynthesizeWithRealStack) {
    // Proves the 2-arg production constructor wires a real SynthesisSession
    // (not a stub), by verifying synthesis completes with non-empty audio.
    if (!network_enabled()) return;

    SpeechSynthesizer c("Hi.", TtsConfig::defaults());
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

TEST(CommunicateNetwork, ProductionConstructorWithProxyRejectsEarly) {
    // Proves the 3-arg production constructor wires SynthesisOptions into
    // SpeechSynthesizer and that proxy is rejected at the API layer before
    // any transport call is made.
    if (!network_enabled()) return;

    SynthesisOptions opts;
    opts.proxy = "http://127.0.0.1:1";
    SpeechSynthesizer c("Hi.", TtsConfig::defaults(), opts);
    auto result = c.synthesize();

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), edge_tts::common::ErrorCode::unsupported);
}
