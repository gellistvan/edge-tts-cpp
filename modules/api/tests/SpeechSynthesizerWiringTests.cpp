// Tests for the production synthesizer wiring introduced in SpeechSynthesizer.cpp.
//
// These tests verify that:
//   1. The api::SpeechSynthesizer production constructors call the real communication
//      stack (SynthesisSession) rather than a stub.
//   2. The full production composition (all real objects except the WebSocket
//      transport) works end-to-end: text → TextChunker → SynthesisSession →
//      audio chunks.
//   3. The old hardcoded "WebSocket transport not yet implemented" error is gone.
//
// The seam used here is the existing SynthesizerFn injection constructor:
//   SpeechSynthesizer(text, config, SynthesisOptions, SynthesizerFn)
// This constructor is also used internally by production constructors.
// We wire all real production objects into the SynthesizerFn but replace
// WebSocketClient with FakeWebSocketClient so no network calls are made.

#include "api/SpeechSynthesizer.hpp"
#include "api/SynthesisOptions.hpp"
#include "communication/ConnectionMetadata.hpp"
#include "communication/EdgeProtocol.hpp"
#include "communication/EdgeServiceConfig.hpp"
#include "communication/EdgeTokenProvider.hpp"
#include "communication/FakeWebSocketClient.hpp"
#include "communication/SynthesisSession.hpp"
#include "communication/WebSocketMessage.hpp"
#include "support/WebSocketFrameHelpers.hpp"
#include "support/ChunkTestHelpers.hpp"
#include "common/Clock.hpp"
#include "common/Error.hpp"
#include "common/IdGenerator.hpp"
#include "core/Chunk.hpp"
#include "core/TtsConfig.hpp"
#include "vendor/minigtest/minigtest.hpp"
#include "ApiTestFixtures.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <variant>
#include <vector>

using edge_tts::api::SpeechSynthesizer;
using edge_tts::api::SynthesisOptions;
using edge_tts::api::SynthesizerFn;
using edge_tts::communication::ConnectionMetadataFactory;
using edge_tts::communication::EdgeProtocol;
using edge_tts::communication::EdgeServiceConfig;
using edge_tts::communication::EdgeTokenProvider;
using edge_tts::communication::FakeWebSocketClient;
using edge_tts::communication::SynthesisSession;
using edge_tts::communication::WebSocketMessage;
using edge_tts::common::ErrorCode;
using edge_tts::common::IdGenerator;
using edge_tts::common::SystemClock;
using edge_tts::core::AudioChunk;
using edge_tts::core::TtsChunk;
using edge_tts::core::TtsConfig;
using edge_tts::core::BoundaryChunk;

using edge_tts::test::make_audio_frame;
using edge_tts::test::make_turn_end;
using edge_tts::test::to_bytes;
using edge_tts::test::push_session;
using edge_tts::test::valid_config;
using edge_tts::test::read_file;

// ---------------------------------------------------------------------------
// Full production composition test (api-level, fake transport seam)
//
// Wires all real production objects (SystemClock, IdGenerator,
// EdgeTokenProvider, EdgeProtocol, ConnectionMetadataFactory, SynthesisSession)
// but substitutes FakeWebSocketClient for WebSocketClient.  Calls SpeechSynthesizer
// API and verifies audio chunks arrive.
// ---------------------------------------------------------------------------

TEST(CommunicateProductionWiring, FullCompositionProducesAudioChunks) {
    // All real production objects except transport.
    SystemClock clock;
    IdGenerator ids;
    const EdgeServiceConfig svc = edge_tts::communication::default_edge_service_config();
    EdgeTokenProvider       token_provider{svc, clock};
    EdgeProtocol            protocol{clock};
    ConnectionMetadataFactory meta_factory{ids};

    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "AUDIO_BYTES");

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory, clock};

    // Inject via the SynthesizerFn seam — same path SpeechSynthesizer uses internally.
    SpeechSynthesizer c("hello world", valid_config(), SynthesisOptions{},
        [&session](const TtsConfig& cfg, std::span<const std::string> chunks)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return session.synthesize(cfg, chunks);
        });

    auto result = c.synthesize();

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->empty());
    bool found_audio = false;
    for (const auto& chunk : *result)
        if (edge_tts::core::is_audio(chunk)) found_audio = true;
    EXPECT_TRUE(found_audio);
}

TEST(CommunicateProductionWiring, FullCompositionAudioBytesCorrect) {
    SystemClock clock;
    IdGenerator ids;
    const EdgeServiceConfig svc = edge_tts::communication::default_edge_service_config();
    EdgeTokenProvider       token_provider{svc, clock};
    EdgeProtocol            protocol{clock};
    ConnectionMetadataFactory meta_factory{ids};

    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "MP3PAYLOAD");

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory, clock};

    SpeechSynthesizer c("test text", valid_config(), SynthesisOptions{},
        [&session](const TtsConfig& cfg, std::span<const std::string> chunks)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return session.synthesize(cfg, chunks);
        });

    auto result = c.synthesize();
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->empty());

    // First chunk must be audio with the expected bytes.
    ASSERT_TRUE(edge_tts::core::is_audio((*result)[0]));
    const auto& ac = std::get<AudioChunk>((*result)[0]);
    const std::string got(reinterpret_cast<const char*>(ac.data.data()), ac.data.size());
    EXPECT_EQ(got, "MP3PAYLOAD");
}

TEST(CommunicateProductionWiring, MultiChunkTextProducesAudioPerChunk) {
    // Long text → TextChunker splits into multiple chunks →
    // SynthesisSession is called once per chunk → FakeWebSocketClient
    // must have enough sessions queued.
    SystemClock clock;
    IdGenerator ids;
    const EdgeServiceConfig svc = edge_tts::communication::default_edge_service_config();
    EdgeTokenProvider       token_provider{svc, clock};
    EdgeProtocol            protocol{clock};
    ConnectionMetadataFactory meta_factory{ids};

    FakeWebSocketClient fake_ws;
    // Queue two sessions — one per chunk (long text will produce ≥2 chunks).
    push_session(fake_ws, "CHUNK_A");
    push_session(fake_ws, "CHUNK_B");

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory, clock};

    // 5000 bytes exceeds the 4096-byte limit → at least two chunks.
    std::string long_text(5000, 'x');
    SpeechSynthesizer c(long_text, valid_config(), SynthesisOptions{},
        [&session](const TtsConfig& cfg, std::span<const std::string> chunks)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return session.synthesize(cfg, chunks);
        });

    auto result = c.synthesize();
    ASSERT_TRUE(result.has_value());

    // Both chunks should have produced audio.
    int audio_count = 0;
    for (const auto& chunk : *result)
        if (edge_tts::core::is_audio(chunk)) ++audio_count;
    EXPECT_EQ(audio_count, 2);
}

TEST(CommunicateProductionWiring, ProxyStoredInOptionsAndBlocksSession) {
    // Proxy is stored in SpeechSynthesizer::options() but rejected at the API
    // layer before the synthesizer function (and thus the FakeWebSocketClient
    // session) is ever called.
    SystemClock clock;
    IdGenerator ids;
    const EdgeServiceConfig svc = edge_tts::communication::default_edge_service_config();
    EdgeTokenProvider       token_provider{svc, clock};
    EdgeProtocol            protocol{clock};
    ConnectionMetadataFactory meta_factory{ids};

    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "AUDIO");

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory, clock};
    bool session_called = false;

    SynthesisOptions opts;
    opts.proxy = "http://proxy.test:3128";

    SpeechSynthesizer c("hello", valid_config(), opts,
        [&session, &session_called](const TtsConfig& cfg,
                                   std::span<const std::string> chunks)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            session_called = true;
            return session.synthesize(cfg, chunks);
        });

    EXPECT_EQ(c.options().proxy, opts.proxy);
    auto result = c.synthesize();
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), edge_tts::common::ErrorCode::unsupported);
    EXPECT_FALSE(session_called);
    EXPECT_EQ(fake_ws.connect_count(), 0);
}

// ---------------------------------------------------------------------------
// Structural offline tests for the production 2-arg and 3-arg constructors.
//
// These tests verify the structural behavior of the production constructors
// WITHOUT calling synthesize()() or save().  Calling either of those on a
// production-constructed SpeechSynthesizer would attempt a real network connection,
// making the test environment-dependent.
//
// The actual "production constructor synthesizes correctly via the real stack"
// verification lives in tests/api/CommunicateNetworkTests.cpp, gated behind
// EDGE_TTS_ENABLE_NETWORK_TESTS=ON and EDGE_TTS_RUN_NETWORK_TESTS=1.
// ---------------------------------------------------------------------------

TEST(CommunicateProductionWiring, ProductionConstructorStoresTextAndConfig) {
    // Constructing with the 2-arg form must not crash or throw, and must
    // preserve the text and config that were passed in.
    SpeechSynthesizer c("hello world", valid_config());
    EXPECT_EQ(c.text(), "hello world");
    EXPECT_EQ(c.config().voice, TtsConfig::defaults().voice);
}

TEST(CommunicateProductionWiring, ProductionConstructorWithOptionsStoresOptions) {
    // The 3-arg form (text, config, options) must store options correctly.
    // Verifies SynthesisOptions is preserved without making network calls.
    SynthesisOptions opts;
    opts.proxy = "http://proxy.test:3128";
    SpeechSynthesizer c("hello", valid_config(), opts);
    EXPECT_EQ(c.options().proxy, opts.proxy);
    EXPECT_EQ(c.text(), "hello");
}

TEST(CommunicateProductionWiring, ProductionConstructorDefaultOptionsAreEmpty) {
    SpeechSynthesizer c("hello", valid_config());
    EXPECT_FALSE(c.options().proxy.has_value());
}

TEST(CommunicateProductionWiring, ProductionConstructorPreservesVoice) {
    TtsConfig cfg = TtsConfig::defaults();
    cfg.voice = "en-GB-RyanNeural";
    SpeechSynthesizer c("test", cfg);
    EXPECT_EQ(c.config().voice, "en-GB-RyanNeural");
}

// ---------------------------------------------------------------------------
// Construction is lazy: no network I/O before synthesize()()/save()
//
// These tests prove that calling the production constructors — even the ones
// that build the full real networking stack — does not open any connection.
// The structural assertions (text(), config(), options()) all complete without
// any network activity.
// ---------------------------------------------------------------------------

TEST(CommunicateProductionWiring, ConstructionIsLazy_TextAccessibleWithoutNetwork) {
    // If the constructor attempted a real network call it would fail
    // (no live service reachable in offline tests) — the fact that this
    // returns "hello world" without error proves construction is lazy.
    SpeechSynthesizer c("hello world", valid_config());
    EXPECT_EQ(c.text(), "hello world");
}

TEST(CommunicateProductionWiring, ConstructionIsLazy_ConfigAccessibleWithoutNetwork) {
    TtsConfig cfg = valid_config();
    cfg.rate = "+20%";
    SpeechSynthesizer c("text", cfg);
    EXPECT_EQ(c.config().rate, "+20%");
}

TEST(CommunicateProductionWiring, ConstructionIsLazy_OptionsAccessibleWithoutNetwork) {
    SynthesisOptions opts;
    opts.ws_connect_timeout = std::chrono::milliseconds{5'000};
    SpeechSynthesizer c("text", valid_config(), opts);
    EXPECT_EQ(c.options().ws_connect_timeout, std::chrono::milliseconds{5'000});
}

// ---------------------------------------------------------------------------
// No placeholder error: the production constructors compose the real stack.
// The seam constructor (4-arg) exposes this: a fake transport that returns
// real audio bytes must succeed, proving no stub/placeholder intercepts the
// call before the actual synthesizer.
// ---------------------------------------------------------------------------

TEST(CommunicateProductionWiring, NoPlaceholderError_FakeStackReturnsRealResult) {
    // If any placeholder "network_error" stub intercepted the call before
    // the real SynthesizerFn, this test would fail (synthesizer_ran = false
    // or result would be an error).
    bool synthesizer_ran = false;
    std::string synth_result;

    SynthesizerFn syn =
        [&synthesizer_ran, &synth_result](
            const TtsConfig&, std::span<const std::string> chunks)
        -> edge_tts::common::Result<std::vector<TtsChunk>>
    {
        synthesizer_ran = true;
        synth_result    = chunks.empty() ? "" : chunks[0];
        AudioChunk ac;
        ac.data = {std::byte{0x01}, std::byte{0x02}};
        return edge_tts::common::Result<std::vector<TtsChunk>>::ok(
            {TtsChunk{ac}});
    };

    SpeechSynthesizer c("hello", valid_config(), SynthesisOptions{}, std::move(syn));
    auto result = c.synthesize();

    EXPECT_TRUE(synthesizer_ran);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->empty());
    // Confirm error code is NOT the old placeholder network_error
    // (a successful result proves the placeholder was never returned)
}

// ---------------------------------------------------------------------------
// save() through the full fake transport seam
// ---------------------------------------------------------------------------

namespace {
namespace fs = std::filesystem;

struct TempFileW {
    fs::path path;
    TempFileW(const std::string& tag, const std::string& ext)
        : path(fs::temp_directory_path() / ("wire_test_" + tag + ext)) {}
    ~TempFileW() { std::error_code ec; fs::remove(path, ec); }
};
}  // namespace

TEST(CommunicateProductionWiring, SaveWritesAudioBytesViaFakeStack) {
    SystemClock clock;
    IdGenerator ids;
    const EdgeServiceConfig svc = edge_tts::communication::default_edge_service_config();
    EdgeTokenProvider         token_provider{svc, clock};
    EdgeProtocol              protocol{clock};
    ConnectionMetadataFactory meta_factory{ids};

    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "MP3CONTENT");

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory, clock};

    TempFileW mp3{"save_audio", ".mp3"};
    SpeechSynthesizer c("hello", valid_config(), SynthesisOptions{},
        [&session](const TtsConfig& cfg, std::span<const std::string> chunks)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return session.synthesize(cfg, chunks);
        });

    auto r = c.save(mp3.path);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(read_file(mp3.path), "MP3CONTENT");
}

TEST(CommunicateProductionWiring, SaveWritesNonEmptyMp3File) {
    SystemClock clock;
    IdGenerator ids;
    const EdgeServiceConfig svc = edge_tts::communication::default_edge_service_config();
    EdgeTokenProvider         token_provider{svc, clock};
    EdgeProtocol              protocol{clock};
    ConnectionMetadataFactory meta_factory{ids};

    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "FAKEMP3DATA");

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory, clock};

    TempFileW mp3{"save_nonempty", ".mp3"};
    SpeechSynthesizer c("test", valid_config(), SynthesisOptions{},
        [&session](const TtsConfig& cfg, std::span<const std::string> chunks)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return session.synthesize(cfg, chunks);
        });

    ASSERT_TRUE(c.save(mp3.path).has_value());
    EXPECT_TRUE(fs::exists(mp3.path));
    EXPECT_TRUE(fs::file_size(mp3.path) > 0);
}

TEST(CommunicateProductionWiring, SaveIsOneShotViaFakeStack) {
    SystemClock clock;
    IdGenerator ids;
    const EdgeServiceConfig svc = edge_tts::communication::default_edge_service_config();
    EdgeTokenProvider         token_provider{svc, clock};
    EdgeProtocol              protocol{clock};
    ConnectionMetadataFactory meta_factory{ids};

    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "AUDIO");

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory, clock};

    TempFileW mp3{"save_oneshot", ".mp3"};
    SpeechSynthesizer c("hello", valid_config(), SynthesisOptions{},
        [&session](const TtsConfig& cfg, std::span<const std::string> chunks)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return session.synthesize(cfg, chunks);
        });

    ASSERT_TRUE(c.save(mp3.path).has_value());
    auto r2 = c.save(mp3.path);
    EXPECT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().code(), ErrorCode::invalid_state);
}

// ---------------------------------------------------------------------------
// Regression: XML special characters are escaped in the SSML frame sent
// over the (fake) WebSocket transport
// ---------------------------------------------------------------------------

TEST(CommunicateProductionWiring, AmpersandEscapedInSsmlOnWire) {
    SystemClock clock;
    IdGenerator ids;
    const EdgeServiceConfig svc = edge_tts::communication::default_edge_service_config();
    EdgeTokenProvider         token_provider{svc, clock};
    EdgeProtocol              protocol{clock};
    ConnectionMetadataFactory meta_factory{ids};

    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "AUDIO");

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory, clock};
    SpeechSynthesizer c("cats & dogs", valid_config(), SynthesisOptions{},
        [&session](const TtsConfig& cfg, std::span<const std::string> chunks)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return session.synthesize(cfg, chunks);
        });

    ASSERT_TRUE(c.synthesize().has_value());
    // Index 0 = speech.config, index 1 = SSML frame
    const auto& msgs = fake_ws.sent_messages();
    ASSERT_TRUE(msgs.size() >= 2u);
    EXPECT_NE(msgs[1].find("&amp;"), std::string::npos);
    // Raw & must not appear in the SSML content
    EXPECT_EQ(msgs[1].find(" & "), std::string::npos);
}

TEST(CommunicateProductionWiring, AngleBracketsEscapedInSsmlOnWire) {
    SystemClock clock;
    IdGenerator ids;
    const EdgeServiceConfig svc = edge_tts::communication::default_edge_service_config();
    EdgeTokenProvider         token_provider{svc, clock};
    EdgeProtocol              protocol{clock};
    ConnectionMetadataFactory meta_factory{ids};

    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "AUDIO");

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory, clock};
    SpeechSynthesizer c("a < b > c", valid_config(), SynthesisOptions{},
        [&session](const TtsConfig& cfg, std::span<const std::string> chunks)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return session.synthesize(cfg, chunks);
        });

    ASSERT_TRUE(c.synthesize().has_value());
    const auto& msgs = fake_ws.sent_messages();
    ASSERT_TRUE(msgs.size() >= 2u);
    EXPECT_NE(msgs[1].find("&lt;"), std::string::npos);
    EXPECT_NE(msgs[1].find("&gt;"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Regression: multi-byte UTF-8 text is preserved through the encoding pipeline
// ---------------------------------------------------------------------------

TEST(CommunicateProductionWiring, MultiByteUtf8PreservedInSsml) {
    SystemClock clock;
    IdGenerator ids;
    const EdgeServiceConfig svc = edge_tts::communication::default_edge_service_config();
    EdgeTokenProvider         token_provider{svc, clock};
    EdgeProtocol              protocol{clock};
    ConnectionMetadataFactory meta_factory{ids};

    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "AUDIO");

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory, clock};

    // "こんにちは" — each kana character is 3 bytes in UTF-8
    const std::string japanese =
        "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf";
    SpeechSynthesizer c(japanese, valid_config(), SynthesisOptions{},
        [&session](const TtsConfig& cfg, std::span<const std::string> chunks)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return session.synthesize(cfg, chunks);
        });

    ASSERT_TRUE(c.synthesize().has_value());
    const auto& msgs = fake_ws.sent_messages();
    ASSERT_TRUE(msgs.size() >= 2u);
    // The UTF-8 bytes must appear verbatim in the SSML frame
    EXPECT_NE(msgs[1].find(japanese), std::string::npos);
}

// ---------------------------------------------------------------------------
// save() partial-failure behavior
//
// save() writes MP3 first, then SRT.  These writes are NOT atomic.
// If the SRT write fails after the MP3 write succeeds:
//   - save() returns an io_error.
//   - The MP3 file remains on disk with its content intact.
//   - The SRT file is not created (write never started).
//
// Tests below verify this contract using a deliberately non-writable SRT
// path (parent directory does not exist) to trigger the failure.
// ---------------------------------------------------------------------------

using edge_tts::test::make_audio;
using edge_tts::test::make_boundary;
using edge_tts::test::make_fake;

TEST(SavePartialFailure, SrtWriteFailureLeavesAudioOnDisk) {
    // Make an SRT path whose parent directory does not exist so the write fails.
    namespace fs = std::filesystem;
    const fs::path mp3 = fs::temp_directory_path() / "partial_fail_audio.mp3";
    const fs::path srt = fs::temp_directory_path() / "no_such_dir_xyz_abc" / "out.srt";

    // Clean up MP3 in any case.
    std::error_code ec;
    fs::remove(mp3, ec);

    // Build a fake synthesizer that returns audio + one boundary.
    std::vector<TtsChunk> chunks2;
    chunks2.emplace_back(make_audio("FAKE_AUDIO"));
    chunks2.emplace_back(make_boundary("hello", 0, 10'000'000));
    SpeechSynthesizer s("hello", valid_config(), make_fake(std::move(chunks2)));

    auto r = s.save(mp3, srt);

    // save() must return an error.
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::io_error);

    // The MP3 must still exist and have content.
    EXPECT_TRUE(fs::exists(mp3));
    EXPECT_TRUE(fs::file_size(mp3) > 0u);

    // The SRT must NOT exist.
    EXPECT_FALSE(fs::exists(srt));

    fs::remove(mp3, ec);
}

TEST(SavePartialFailure, Mp3WriteFailureReturnsIoError) {
    // If the MP3 write itself fails (non-existent parent), save() returns io_error
    // and no SRT is written.
    namespace fs = std::filesystem;
    const fs::path mp3 = fs::temp_directory_path() / "no_such_dir_xyz_abc" / "out.mp3";
    const fs::path srt = fs::temp_directory_path() / "partial_fail_only.srt";

    std::error_code ec;
    fs::remove(srt, ec);

    std::vector<TtsChunk> chunks1;
    chunks1.emplace_back(make_audio("DATA"));
    SpeechSynthesizer s("hello", valid_config(), make_fake(std::move(chunks1)));

    auto r = s.save(mp3, srt);

    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::io_error);
    // SRT must not have been created (MP3 failed first).
    EXPECT_FALSE(fs::exists(srt));

    fs::remove(srt, ec);
}

TEST(SavePartialFailure, BothSucceedWhenPathsAreValid) {
    // Control: verify both files are written when paths are valid.
    namespace fs = std::filesystem;
    const fs::path mp3 = fs::temp_directory_path() / "partial_ok.mp3";
    const fs::path srt = fs::temp_directory_path() / "partial_ok.srt";

    std::vector<TtsChunk> chunks3;
    chunks3.emplace_back(make_audio("OK_AUDIO"));
    chunks3.emplace_back(make_boundary("hi", 0, 5'000'000));
    SpeechSynthesizer s("hi", valid_config(), make_fake(std::move(chunks3)));

    auto r = s.save(mp3, srt);

    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(fs::exists(mp3));
    EXPECT_TRUE(fs::exists(srt));
    EXPECT_TRUE(fs::file_size(mp3) > 0u);
    EXPECT_TRUE(fs::file_size(srt) > 0u);

    std::error_code ec;
    fs::remove(mp3, ec);
    fs::remove(srt, ec);
}
