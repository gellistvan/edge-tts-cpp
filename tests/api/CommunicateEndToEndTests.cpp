// Offline end-to-end integration tests for api::Communicate.
//
// These tests exercise the full pipeline from text input — through TextChunker,
// SynthesisSession, EdgeProtocol, and FakeWebSocketClient — to file output, with
// no real network calls.  All transport I/O is handled by FakeWebSocketClient with
// pre-queued frames.
//
// Testing pyramid placement: **offline integration** (between unit tests and real-
// network tests).  They exercise more layers than CommunicateTests.cpp (which uses
// the SynthesizerFn seam) and prove that the full stack wired together behaves
// correctly without requiring a live service connection.
//
// What these tests cover that unit tests do not:
//   - save() writing both MP3 and SRT through the full stack (acceptance criterion)
//   - XML special characters escaped in the actual SSML frame on the wire
//   - Multi-byte UTF-8 text preserved through the full encoding pipeline
//   - Multi-chunk long text producing combined MP3 and multi-chunk SRT

#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/api/CommunicateOptions.hpp"
#include "edge_tts/communication/ConnectionMetadata.hpp"
#include "edge_tts/communication/EdgeProtocol.hpp"
#include "edge_tts/communication/EdgeServiceConfig.hpp"
#include "edge_tts/communication/EdgeTokenProvider.hpp"
#include "edge_tts/communication/FakeWebSocketClient.hpp"
#include "edge_tts/communication/SynthesisSession.hpp"
#include "edge_tts/communication/WebSocketMessage.hpp"
#include "communication/WebSocketFrameHelpers.hpp"
#include "edge_tts/common/Clock.hpp"
#include "edge_tts/common/Error.hpp"
#include "edge_tts/common/IdGenerator.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

using edge_tts::api::Communicate;
using edge_tts::api::CommunicateOptions;
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
using edge_tts::core::BoundaryChunk;
using edge_tts::core::TtsChunk;
using edge_tts::core::TtsConfig;

namespace {

using edge_tts::test::make_audio_frame;
using edge_tts::test::make_turn_end;
using edge_tts::test::make_word_boundary;

// Push a minimal session: one audio frame followed by turn.end.
static void push_session(FakeWebSocketClient& ws, const std::string& audio) {
    ws.push_incoming(make_audio_frame(audio));
    ws.push_incoming(make_turn_end());
}

struct BoundarySpec { int64_t offset; int64_t duration; std::string word; };

// Push a session that emits audio, then word boundaries, then turn.end.
static void push_session_with_boundaries(FakeWebSocketClient& ws,
                                          const std::string& audio,
                                          const std::vector<BoundarySpec>& bounds) {
    ws.push_incoming(make_audio_frame(audio));
    for (const auto& b : bounds)
        ws.push_incoming(make_word_boundary(b.offset, b.duration, b.word));
    ws.push_incoming(make_turn_end());
}

// RAII temp-file cleanup.
struct TempFile {
    fs::path path;
    explicit TempFile(const std::string& tag, const std::string& ext)
        : path(fs::temp_directory_path() / ("e2e_test_" + tag + ext)) {}
    ~TempFile() { std::error_code ec; fs::remove(path, ec); }
};

// Holds all production-wiring objects that SynthesisSession needs.
// Members are in initialization order (objects that are referenced by others
// must be declared first so references are valid at construction time).
struct ProductionWiring {
    SystemClock               clock;
    IdGenerator               ids;
    EdgeServiceConfig         svc{edge_tts::communication::default_edge_service_config()};
    EdgeTokenProvider         tokens{svc, clock};
    EdgeProtocol              protocol{clock};
    ConnectionMetadataFactory meta{ids};
};

// Build a SynthesizerFn that delegates to the given SynthesisSession.
// The session must outlive the returned SynthesizerFn.
static SynthesizerFn make_seam(SynthesisSession& session) {
    return [&session](const TtsConfig& cfg,
                      std::span<const std::string> chunks)
               -> edge_tts::common::Result<std::vector<TtsChunk>> {
        return session.synthesize(cfg, chunks);
    };
}

// Read the entire contents of a file as a string.
static std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>{}};
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Acceptance criterion: save() writes both MP3 and SRT files
// ---------------------------------------------------------------------------

TEST(CommunicateEndToEnd, SaveWritesBothMp3AndSrt) {
    // Full production stack (all real objects) with fake transport.
    // Queued frames: audio + two word boundaries + turn.end.
    ProductionWiring   w;
    FakeWebSocketClient fake_ws;
    push_session_with_boundaries(fake_ws, "FAKEMP3BYTES",
        { {50'000'000LL, 5'000'000LL, "Hello"},
          {150'000'000LL, 5'000'000LL, "world"} });

    SynthesisSession session{fake_ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    TempFile mp3{"save_both", ".mp3"};
    TempFile srt{"save_both", ".srt"};

    Communicate c("Hello world", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    auto r = c.save(mp3.path, srt.path);

    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(fs::exists(mp3.path));
    EXPECT_TRUE(fs::exists(srt.path));
    EXPECT_TRUE(fs::file_size(mp3.path) > 0u);
    EXPECT_TRUE(fs::file_size(srt.path) > 0u);
}

// ---------------------------------------------------------------------------
// MP3 bytes: save() writes audio bytes verbatim from the fake session
// ---------------------------------------------------------------------------

TEST(CommunicateEndToEnd, SaveMp3BytesMatchFakeAudio) {
    ProductionWiring   w;
    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "EXPECTEDMP3BYTES");

    SynthesisSession session{fake_ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    TempFile mp3{"mp3_bytes", ".mp3"};
    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.save(mp3.path).has_value());

    EXPECT_EQ(read_file(mp3.path), "EXPECTEDMP3BYTES");
}

// ---------------------------------------------------------------------------
// SRT content: word text from boundary events appears in the subtitle file
// ---------------------------------------------------------------------------

TEST(CommunicateEndToEnd, SaveSrtContainsWordText) {
    ProductionWiring   w;
    FakeWebSocketClient fake_ws;
    push_session_with_boundaries(fake_ws, "AUDIO",
        { {10'000'000LL, 3'000'000LL, "sunshine"} });

    SynthesisSession session{fake_ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    TempFile mp3{"srt_word", ".mp3"};
    TempFile srt{"srt_word", ".srt"};
    Communicate c("sunshine", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.save(mp3.path, srt.path).has_value());

    const std::string content = read_file(srt.path);
    EXPECT_NE(content.find("sunshine"), std::string::npos);
}

TEST(CommunicateEndToEnd, SaveSrtHasTimestampAndArrow) {
    // SRT must have the standard "HH:MM:SS,mmm --> HH:MM:SS,mmm" timestamp format.
    ProductionWiring   w;
    FakeWebSocketClient fake_ws;
    push_session_with_boundaries(fake_ws, "AUDIO",
        { {10'000'000LL, 5'000'000LL, "hello"},
          {100'000'000LL, 5'000'000LL, "world"} });

    SynthesisSession session{fake_ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    TempFile mp3{"srt_fmt", ".mp3"};
    TempFile srt{"srt_fmt", ".srt"};
    Communicate c("hello world", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.save(mp3.path, srt.path).has_value());

    const std::string content = read_file(srt.path);
    EXPECT_NE(content.find("-->"), std::string::npos);
    EXPECT_NE(content.find("hello"), std::string::npos);
    EXPECT_NE(content.find("world"), std::string::npos);
}

// ---------------------------------------------------------------------------
// stream_sync(): full stack returns both audio and boundary chunks
// ---------------------------------------------------------------------------

TEST(CommunicateEndToEnd, StreamSyncReturnsBothAudioAndBoundaryChunks) {
    ProductionWiring   w;
    FakeWebSocketClient fake_ws;
    push_session_with_boundaries(fake_ws, "AUDIODATA",
        { {50'000'000LL, 5'000'000LL, "hello"} });

    SynthesisSession session{fake_ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
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
// XML special characters: &, <, > are escaped in the actual SSML frame
// on the wire.  Checks fake_ws.sent_messages()[1] (the SSML text frame,
// after speech.config at index [0]).
// ---------------------------------------------------------------------------

TEST(CommunicateEndToEnd, AmpersandEscapedInSsmlFrame) {
    ProductionWiring   w;
    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "AUDIO");

    SynthesisSession session{fake_ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("cats & dogs", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    // sent_messages()[0] = speech.config; [1] = SSML frame
    const auto& msgs = fake_ws.sent_messages();
    ASSERT_TRUE(msgs.size() >= 2u);
    EXPECT_NE(msgs[1].find("&amp;"), std::string::npos);
}

TEST(CommunicateEndToEnd, LessThanEscapedInSsmlFrame) {
    ProductionWiring   w;
    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "AUDIO");

    SynthesisSession session{fake_ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("a < b", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    const auto& msgs = fake_ws.sent_messages();
    ASSERT_TRUE(msgs.size() >= 2u);
    EXPECT_NE(msgs[1].find("&lt;"), std::string::npos);
}

TEST(CommunicateEndToEnd, GreaterThanEscapedInSsmlFrame) {
    ProductionWiring   w;
    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "AUDIO");

    SynthesisSession session{fake_ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("a > b", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    const auto& msgs = fake_ws.sent_messages();
    ASSERT_TRUE(msgs.size() >= 2u);
    EXPECT_NE(msgs[1].find("&gt;"), std::string::npos);
}

TEST(CommunicateEndToEnd, AllXmlSpecialCharsEscapedTogether) {
    // A single text containing &, <, > — all must appear escaped in SSML.
    ProductionWiring   w;
    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "AUDIO");

    SynthesisSession session{fake_ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("x & y < z > w", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    const auto& msgs = fake_ws.sent_messages();
    ASSERT_TRUE(msgs.size() >= 2u);
    const std::string& ssml = msgs[1];
    EXPECT_NE(ssml.find("&amp;"), std::string::npos);
    EXPECT_NE(ssml.find("&lt;"),  std::string::npos);
    EXPECT_NE(ssml.find("&gt;"),  std::string::npos);
    EXPECT_EQ(ssml.find(" & "), std::string::npos);
}

// ---------------------------------------------------------------------------
// Multi-byte UTF-8: Japanese text passes through the encoding pipeline
// unchanged and appears verbatim in the SSML frame
// ---------------------------------------------------------------------------

TEST(CommunicateEndToEnd, MultiByteUtf8PreservedInSsmlFrame) {
    ProductionWiring   w;
    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "AUDIO");

    SynthesisSession session{fake_ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    // "こんにちは" — each character is 3 bytes in UTF-8.
    const std::string japanese =
        "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf";
    Communicate c(japanese, TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    const auto& msgs = fake_ws.sent_messages();
    ASSERT_TRUE(msgs.size() >= 2u);
    EXPECT_NE(msgs[1].find(japanese), std::string::npos);
}

// ---------------------------------------------------------------------------
// Long text (> 4096 bytes): TextChunker splits into ≥2 chunks.
// save() concatenates audio from all chunks into one MP3 file.
// ---------------------------------------------------------------------------

TEST(CommunicateEndToEnd, LongTextSavesCombinedMp3) {
    ProductionWiring   w;
    FakeWebSocketClient fake_ws;
    // Queue two sessions — one per chunk produced by the 5000-byte input.
    push_session(fake_ws, "CHUNK_A_AUDIO");
    push_session(fake_ws, "CHUNK_B_AUDIO");

    SynthesisSession session{fake_ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    TempFile mp3{"long_mp3", ".mp3"};
    std::string long_text(5000, 'x');  // exceeds 4096-byte chunk limit
    Communicate c(long_text, TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.save(mp3.path).has_value());

    const std::string content = read_file(mp3.path);
    EXPECT_NE(content.find("CHUNK_A_AUDIO"), std::string::npos);
    EXPECT_NE(content.find("CHUNK_B_AUDIO"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Long text with boundaries: SRT spans boundaries from all chunks
// ---------------------------------------------------------------------------

TEST(CommunicateEndToEnd, LongTextSrtSpansAllChunks) {
    ProductionWiring   w;
    FakeWebSocketClient fake_ws;
    push_session_with_boundaries(fake_ws, "CHUNK_A",
        { {10'000'000LL, 3'000'000LL, "first"} });
    push_session_with_boundaries(fake_ws, "CHUNK_B",
        { {10'000'000LL, 3'000'000LL, "second"} });

    SynthesisSession session{fake_ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    TempFile mp3{"long_srt", ".mp3"};
    TempFile srt{"long_srt", ".srt"};
    std::string long_text(5000, 'x');
    Communicate c(long_text, TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.save(mp3.path, srt.path).has_value());

    const std::string content = read_file(srt.path);
    // Boundaries from both chunks must appear in the combined SRT.
    EXPECT_NE(content.find("first"),  std::string::npos);
    EXPECT_NE(content.find("second"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Long text via stream_sync(): all chunks return their audio chunks
// ---------------------------------------------------------------------------

TEST(CommunicateEndToEnd, LongTextStreamSyncReturnsAudioPerChunk) {
    ProductionWiring   w;
    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "CHUNK_A");
    push_session(fake_ws, "CHUNK_B");

    SynthesisSession session{fake_ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    std::string long_text(5000, 'x');
    Communicate c(long_text, TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    auto result = c.stream_sync();

    ASSERT_TRUE(result.has_value());
    int audio_count = 0;
    for (const auto& chunk : *result)
        if (std::holds_alternative<AudioChunk>(chunk)) ++audio_count;
    // Two chunks → two AudioChunk events.
    EXPECT_EQ(audio_count, 2);
}

// ---------------------------------------------------------------------------
// Single-use guarantee through the full stack
// ---------------------------------------------------------------------------

TEST(CommunicateEndToEnd, StreamSyncIsOneShot) {
    ProductionWiring   w;
    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "AUDIO");

    SynthesisSession session{fake_ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    (void)c.stream_sync();

    auto r2 = c.stream_sync();
    EXPECT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().code(), ErrorCode::invalid_state);
}

TEST(CommunicateEndToEnd, SaveIsOneShot) {
    ProductionWiring   w;
    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "AUDIO");

    SynthesisSession session{fake_ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    TempFile mp3{"one_shot", ".mp3"};
    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.save(mp3.path).has_value());

    auto r2 = c.save(mp3.path);
    EXPECT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().code(), ErrorCode::invalid_state);
}
