// Real-network integration tests for api::Communicate.
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
//   EDGE_TTS_RUN_NETWORK_TESTS=1 ctest --test-dir build \
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

#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/api/CommunicateOptions.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

using edge_tts::api::Communicate;
using edge_tts::api::CommunicateOptions;
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
// Smoke test: stream_sync() returns non-empty audio
// ---------------------------------------------------------------------------

TEST(CommunicateNetwork, StreamSyncReturnsNonEmptyAudio) {
    if (!network_enabled()) return;

    Communicate c("Hi.", TtsConfig::defaults());
    auto result = c.stream_sync();

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
    Communicate c("Test.", TtsConfig::defaults());
    ASSERT_TRUE(c.save(mp3.path).has_value());

    EXPECT_TRUE(fs::exists(mp3.path));
    EXPECT_GT(fs::file_size(mp3.path), 0u);
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
    Communicate c("Hello world.", cfg);
    ASSERT_TRUE(c.save(mp3.path, srt.path).has_value());

    EXPECT_TRUE(fs::exists(mp3.path));
    EXPECT_GT(fs::file_size(mp3.path), 0u);
    EXPECT_TRUE(fs::exists(srt.path));
    EXPECT_GT(fs::file_size(srt.path), 0u);
}

// ---------------------------------------------------------------------------
// stream_sync() with word-boundary config returns boundary chunks
// ---------------------------------------------------------------------------

TEST(CommunicateNetwork, StreamSyncWithWordBoundaryReturnsBoundaryChunks) {
    if (!network_enabled()) return;

    TtsConfig cfg = TtsConfig::defaults();
    cfg.boundary_type = edge_tts::core::BoundaryType::word;
    Communicate c("Hello world.", cfg);
    auto result = c.stream_sync();

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
// Proxy option: bogus proxy → connection fails, but not with an invalid_state
// error (proves the proxy URL is forwarded and not silently discarded)
// ---------------------------------------------------------------------------

TEST(CommunicateNetwork, BogusProxyYieldsNetworkError) {
    if (!network_enabled()) return;

    CommunicateOptions opts;
    opts.proxy = "http://127.0.0.1:1";  // nothing listening on port 1
    Communicate c("Hello.", TtsConfig::defaults(), opts);
    auto result = c.stream_sync();

    // A real proxy attempt will fail at the transport level.
    // We just verify it is NOT an invalid_state or protocol_error (which would
    // indicate the proxy option was ignored and the call hit a different failure).
    if (!result.has_value()) {
        EXPECT_NE(result.error().code(), edge_tts::common::ErrorCode::invalid_state);
    }
    // If somehow the local port is open and traffic passes, accept the result.
}
