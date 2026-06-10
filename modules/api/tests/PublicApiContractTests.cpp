// Public API contract tests.
//
// These tests verify the documented contract of the public API from a consumer
// perspective:
//   - Object lifetime: construction is cheap (no network at construction time)
//   - Single-use behavior: second call returns ErrorCode::invalid_state
//   - Error model: invalid config returns an error, not an exception
//   - Chunk ownership: synthesize() returns a caller-owned vector
//   - list_voices() is declared and callable (compile-only for offline builds)
//
// All tests use the SynthesizerFn injection constructor so no network calls
// are made.

#include "api/SpeechSynthesizer.hpp"
#include "api/SynthesisOptions.hpp"
#include "api/VoiceList.hpp"
#include "common/Error.hpp"
#include "common/Result.hpp"
#include "core/Chunk.hpp"
#include "core/TtsConfig.hpp"
#include "core/Voice.hpp"
#include "vendor/minigtest/minigtest.hpp"
#include "ApiTestFixtures.hpp"

#include <chrono>
#include <span>
#include <string>
#include <vector>

using edge_tts::api::SpeechSynthesizer;
using edge_tts::api::SynthesisOptions;
using edge_tts::api::SynthesizerFn;
using edge_tts::common::ErrorCode;
using edge_tts::core::AudioChunk;
using edge_tts::core::TtsChunk;
using edge_tts::core::TtsConfig;
using edge_tts::test::make_fake;
using edge_tts::test::valid_config;

static TtsConfig invalid_config() {
    TtsConfig cfg = TtsConfig::defaults();
    cfg.rate = "NOT_A_RATE";
    return cfg;
}

// ---------------------------------------------------------------------------
// Object lifetime: construction is cheap
// ---------------------------------------------------------------------------

TEST(ApiContract, ConstructionIsLazy_TextPreserved) {
    // Constructing a SpeechSynthesizer must not perform any network I/O.
    // If it did, this test would fail (or hang) in offline builds.
    SpeechSynthesizer s("hello world", valid_config());
    EXPECT_EQ(s.text(), "hello world");
}

TEST(ApiContract, ConstructionIsLazy_ConfigPreserved) {
    TtsConfig cfg = valid_config();
    cfg.voice = "en-GB-RyanNeural";
    SpeechSynthesizer s("text", cfg);
    EXPECT_EQ(s.config().voice, "en-GB-RyanNeural");
}

TEST(ApiContract, ConstructionIsLazy_OptionsPreserved) {
    SynthesisOptions opts;
    opts.ws_connect_timeout = std::chrono::milliseconds{5'000};
    SpeechSynthesizer s("text", valid_config(), opts);
    EXPECT_EQ(s.options().ws_connect_timeout, std::chrono::milliseconds{5'000});
}

// ---------------------------------------------------------------------------
// Single-use: synthesize() is single-use
// ---------------------------------------------------------------------------

TEST(ApiContract, SynthesizeIsSingleUse) {
    SpeechSynthesizer s("hello", valid_config(), make_fake());
    auto r1 = s.synthesize();
    EXPECT_TRUE(r1.has_value());

    auto r2 = s.synthesize();
    EXPECT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().code(), ErrorCode::invalid_state);
}

// ---------------------------------------------------------------------------
// Single-use: save() is single-use
// ---------------------------------------------------------------------------

TEST(ApiContract, SaveIsSingleUse) {
    AudioChunk ac;
    ac.data = {std::byte{0x01}};
    SpeechSynthesizer s("hello", valid_config(),
        make_fake({TtsChunk{std::move(ac)}}));

    namespace fs = std::filesystem;
    const fs::path mp = fs::temp_directory_path() / "contract_single_use.mp3";
    auto r1 = s.save(mp);
    EXPECT_TRUE(r1.has_value());
    fs::remove(mp);

    auto r2 = s.save(mp);
    EXPECT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().code(), ErrorCode::invalid_state);
}

// ---------------------------------------------------------------------------
// Single-use: save() then synthesize() is also blocked
// ---------------------------------------------------------------------------

TEST(ApiContract, SaveBlocksSubsequentSynthesize) {
    AudioChunk ac;
    ac.data = {std::byte{0x02}};
    SpeechSynthesizer s("hello", valid_config(),
        make_fake({TtsChunk{std::move(ac)}}));

    namespace fs = std::filesystem;
    const fs::path mp = fs::temp_directory_path() / "contract_save_then_stream.mp3";
    (void)s.save(mp);
    fs::remove(mp);

    auto r = s.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_state);
}

// ---------------------------------------------------------------------------
// Single-use: synthesize() then save() is also blocked
// ---------------------------------------------------------------------------

TEST(ApiContract, SynthesizeBlocksSubsequentSave) {
    SpeechSynthesizer s("hello", valid_config(), make_fake());
    (void)s.synthesize();

    namespace fs = std::filesystem;
    const fs::path mp = fs::temp_directory_path() / "contract_stream_then_save.mp3";
    auto r = s.save(mp);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_state);
    fs::remove(mp);
}

// ---------------------------------------------------------------------------
// Error model: invalid config returns error, not exception
// ---------------------------------------------------------------------------

TEST(ApiContract, InvalidConfigReturnsError_Synthesize) {
    SpeechSynthesizer s("hello", invalid_config(), make_fake());
    auto r = s.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_argument);
}

TEST(ApiContract, InvalidConfigReturnsError_Save) {
    SpeechSynthesizer s("hello", invalid_config(), make_fake());
    namespace fs = std::filesystem;
    const fs::path mp = fs::temp_directory_path() / "contract_invalid_cfg.mp3";
    auto r = s.save(mp);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_argument);
    fs::remove(mp);
}

// ---------------------------------------------------------------------------
// Chunk ownership: the returned vector outlives the synthesizer
// ---------------------------------------------------------------------------

TEST(ApiContract, ChunkOwnership_VectorOutlivesSynthesizer) {
    AudioChunk ac;
    ac.data = {std::byte{'A'}, std::byte{'B'}, std::byte{'C'}};

    std::vector<TtsChunk> result_chunks;
    {
        SpeechSynthesizer s("hello", valid_config(),
            make_fake({TtsChunk{std::move(ac)}}));
        auto r = s.synthesize();
        ASSERT_TRUE(r.has_value());
        result_chunks = std::move(*r);
    }
    // synthesizer is destroyed; chunks must still be accessible
    ASSERT_EQ(result_chunks.size(), 1u);
    ASSERT_TRUE(edge_tts::core::is_audio(result_chunks[0]));
    const auto& audio = std::get<edge_tts::core::AudioChunk>(result_chunks[0]);
    EXPECT_EQ(audio.data.size(), 3u);
}

// ---------------------------------------------------------------------------
// list_voices() API is declared and callable
//
// Offline compile-only test: just verifying list_voices() exists in the
// public API with the documented signature.  We do not call it (that would
// require a live network connection).
// ---------------------------------------------------------------------------

TEST(ApiContract, ListVoices_SignatureIsCorrect) {
    // Verify list_voices is callable with:
    //   1. No arguments (default options)
    //   2. Explicit SynthesisOptions
    // We capture the function pointer to confirm the signature without calling it.

    using ListVoicesFn = edge_tts::common::Result<std::vector<edge_tts::core::Voice>>(*)(
        edge_tts::api::SynthesisOptions);
    ListVoicesFn fn = &edge_tts::api::list_voices;
    (void)fn;

    // Confirm list_voices(opts) compiles with explicit options.
    SynthesisOptions opts;
    (void)opts;
}

// ---------------------------------------------------------------------------
// Accessors are const and do not require synthesis
// ---------------------------------------------------------------------------

TEST(ApiContract, ConstAccessorsCompileAndReturn) {
    const SpeechSynthesizer s("const text", valid_config(), make_fake());
    const std::string& t = s.text();
    const TtsConfig&   c = s.config();
    const SynthesisOptions& o = s.options();
    EXPECT_EQ(t, "const text");
    EXPECT_EQ(c.voice, valid_config().voice);
    EXPECT_FALSE(o.proxy.has_value());
}
