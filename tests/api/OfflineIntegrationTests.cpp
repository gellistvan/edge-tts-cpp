// Offline end-to-end integration tests: Communicate → SynthesisSession →
// EdgeProtocol → FakeWebSocketClient → FileWriter.
//
// These tests complement CommunicateEndToEndTests.cpp.  Where that file
// exercises the happy-path output (audio bytes, SRT content), this file
// focuses on:
//
//   - Frame structure verification: the client really sends speech.config
//     then ssml, with correct Path headers and well-formed X-RequestId.
//   - Escaping correctness: "Tom & Jerry <test>" is escaped exactly once
//     (not double-escaped on a second pass).
//   - Multi-chunk offset compensation at the Communicate level (the second
//     chunk's word-boundary ticks are shifted by the audio duration of the
//     first chunk).
//   - Error propagation: protocol errors and no-audio responses from the
//     fake server surface as the correct ErrorCode at the Communicate API.
//
// All tests use FakeWebSocketClient — no real network, no live service.

#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/api/CommunicateOptions.hpp"
#include "edge_tts/communication/ConnectionMetadata.hpp"
#include "edge_tts/communication/EdgeProtocol.hpp"
#include "edge_tts/communication/EdgeServiceConfig.hpp"
#include "edge_tts/communication/EdgeTokenProvider.hpp"
#include "edge_tts/communication/FakeWebSocketClient.hpp"
#include "edge_tts/communication/SynthesisSession.hpp"
#include "edge_tts/communication/WebSocketMessage.hpp"
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
using edge_tts::common::Error;
using edge_tts::common::ErrorCode;
using edge_tts::common::IdGenerator;
using edge_tts::common::SystemClock;
using edge_tts::core::AudioChunk;
using edge_tts::core::BoundaryChunk;
using edge_tts::core::TtsChunk;
using edge_tts::core::TtsConfig;

// ---------------------------------------------------------------------------
// Shared fixtures
// ---------------------------------------------------------------------------

namespace {

// All production-wiring objects except the WebSocket transport.
// Members are declared in initialization order (referenced objects first).
struct Wire {
    SystemClock               clock;
    IdGenerator               ids;
    EdgeServiceConfig         svc{edge_tts::communication::default_edge_service_config()};
    EdgeTokenProvider         tokens{svc, clock};
    EdgeProtocol              protocol{clock};
    ConnectionMetadataFactory meta{ids};
};

// Build a SynthesizerFn that delegates to the given SynthesisSession.
// The session must outlive the returned fn.
static SynthesizerFn make_seam(SynthesisSession& session) {
    return [&session](const TtsConfig& cfg,
                      std::span<const std::string> chunks)
               -> edge_tts::common::Result<std::vector<TtsChunk>> {
        return session.synthesize(cfg, chunks);
    };
}

// ---------------------------------------------------------------------------
// Frame builders
// ---------------------------------------------------------------------------

// Binary audio frame: 2-byte big-endian header length, header, \r\n, body.
static WebSocketMessage make_audio_frame(const std::string& body) {
    const std::string hdr =
        "X-RequestId:abc\r\nPath:audio\r\nContent-Type:audio/mpeg";
    const auto hl = static_cast<uint16_t>(2 + hdr.size());
    std::vector<std::byte> frame;
    frame.reserve(2 + hdr.size() + 2 + body.size());
    frame.push_back(static_cast<std::byte>(hl >> 8));
    frame.push_back(static_cast<std::byte>(hl & 0xff));
    for (char c : hdr)  frame.push_back(static_cast<std::byte>(c));
    frame.push_back(static_cast<std::byte>('\r'));
    frame.push_back(static_cast<std::byte>('\n'));
    for (char c : body) frame.push_back(static_cast<std::byte>(c));
    WebSocketMessage m;
    m.type   = WebSocketMessage::Type::binary;
    m.binary = std::move(frame);
    return m;
}

// Binary audio frame from a raw byte vector (for size-controlled offset tests).
static WebSocketMessage make_audio_frame_bytes(std::size_t n_bytes) {
    const std::string hdr =
        "X-RequestId:abc\r\nPath:audio\r\nContent-Type:audio/mpeg";
    const auto hl = static_cast<uint16_t>(2 + hdr.size());
    std::vector<std::byte> frame;
    frame.reserve(2 + hdr.size() + 2 + n_bytes);
    frame.push_back(static_cast<std::byte>(hl >> 8));
    frame.push_back(static_cast<std::byte>(hl & 0xff));
    for (char c : hdr)  frame.push_back(static_cast<std::byte>(c));
    frame.push_back(static_cast<std::byte>('\r'));
    frame.push_back(static_cast<std::byte>('\n'));
    for (std::size_t i = 0; i < n_bytes; ++i)
        frame.push_back(std::byte{0xAB});
    WebSocketMessage m;
    m.type   = WebSocketMessage::Type::binary;
    m.binary = std::move(frame);
    return m;
}

static WebSocketMessage make_word_boundary(int64_t offset_ticks,
                                            int64_t duration_ticks,
                                            const std::string& word) {
    WebSocketMessage m;
    m.type = WebSocketMessage::Type::text;
    m.text =
        std::string{"X-RequestId:abc\r\nPath:audio.metadata\r\n\r\n"
                    "{\"Metadata\":[{\"Type\":\"WordBoundary\","
                    "\"Data\":{\"Offset\":"} +
        std::to_string(offset_ticks) +
        ",\"Duration\":" + std::to_string(duration_ticks) +
        ",\"text\":{\"Text\":\"" + word + "\"}}}]}";
    return m;
}

static WebSocketMessage make_turn_end() {
    WebSocketMessage m;
    m.type = WebSocketMessage::Type::text;
    m.text = "X-RequestId:abc\r\nPath:turn.end\r\n\r\n";
    return m;
}

// A text frame with an unknown Path value — parse_incoming must reject it.
static WebSocketMessage make_unknown_path_frame() {
    WebSocketMessage m;
    m.type = WebSocketMessage::Type::text;
    m.text = "X-RequestId:abc\r\nPath:not.a.real.path\r\n\r\n{}";
    return m;
}

// Queue a minimal successful session (audio + turn.end).
static void push_session(FakeWebSocketClient& ws, const std::string& audio) {
    ws.push_incoming(make_audio_frame(audio));
    ws.push_incoming(make_turn_end());
}

// RAII temp-file.
struct TempFile {
    fs::path path;
    explicit TempFile(const std::string& tag, const std::string& ext)
        : path(fs::temp_directory_path() / ("offline_int_" + tag + ext)) {}
    ~TempFile() { std::error_code ec; fs::remove(path, ec); }
};

static std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>{}};
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// 1. Short text produces exactly one audio chunk
// ---------------------------------------------------------------------------

TEST(OfflineIntegration, ShortTextProducesOneAudioChunk) {
    Wire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIODATA");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    auto result = c.stream_sync();

    ASSERT_TRUE(result.has_value());
    int audio_count = 0;
    for (const auto& chunk : *result)
        if (std::holds_alternative<AudioChunk>(chunk)) ++audio_count;
    EXPECT_EQ(audio_count, 1);
}

TEST(OfflineIntegration, ShortTextAudioBytesMatchFakePayload) {
    Wire w;
    FakeWebSocketClient ws;
    push_session(ws, "HELLO_BYTES");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    auto result = c.stream_sync();

    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->empty());
    ASSERT_TRUE(std::holds_alternative<AudioChunk>((*result)[0]));
    const auto& ac = std::get<AudioChunk>((*result)[0]);
    const std::string got(reinterpret_cast<const char*>(ac.data.data()), ac.data.size());
    EXPECT_EQ(got, "HELLO_BYTES");
}

// ---------------------------------------------------------------------------
// 2. Frame structure: client sends speech.config then ssml with correct headers
// ---------------------------------------------------------------------------

TEST(OfflineIntegration, SpeechConfigFrameHasSpeechConfigPath) {
    Wire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    const auto& msgs = ws.sent_messages();
    // Two frames per chunk: speech.config at index 0, ssml at index 1.
    ASSERT_TRUE(msgs.size() >= 2u);
    EXPECT_NE(msgs[0].find("Path:speech.config"), std::string::npos);
}

TEST(OfflineIntegration, SpeechConfigFrameHasContentTypeJson) {
    Wire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    const auto& msgs = ws.sent_messages();
    ASSERT_TRUE(msgs.size() >= 1u);
    EXPECT_NE(msgs[0].find("Content-Type:application/json"), std::string::npos);
}

TEST(OfflineIntegration, SsmlFrameHasSsmlPath) {
    Wire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    const auto& msgs = ws.sent_messages();
    ASSERT_TRUE(msgs.size() >= 2u);
    EXPECT_NE(msgs[1].find("Path:ssml"), std::string::npos);
}

TEST(OfflineIntegration, SsmlFrameHasXRequestIdHeader) {
    Wire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    const auto& msgs = ws.sent_messages();
    ASSERT_TRUE(msgs.size() >= 2u);
    EXPECT_NE(msgs[1].find("X-RequestId:"), std::string::npos);
}

TEST(OfflineIntegration, SsmlFrameRequestIdIs32CharHex) {
    Wire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    const auto& ssml = ws.sent_messages()[1];
    const std::string key = "X-RequestId:";
    const auto pos = ssml.find(key);
    ASSERT_NE(pos, std::string::npos);
    const std::string after = ssml.substr(pos + key.size());
    // Extract until first non-hex char (\r, \n, or space).
    std::string id;
    for (char c : after) {
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
            id += c;
        else
            break;
    }
    // The Python reference uses 32-char UUID hex (no hyphens).
    EXPECT_EQ(id.size(), 32u);
}

TEST(OfflineIntegration, SsmlFrameBodyContainsSpeakTag) {
    Wire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    const auto& ssml = ws.sent_messages()[1];
    EXPECT_NE(ssml.find("<speak"), std::string::npos);
    EXPECT_NE(ssml.find("</speak>"), std::string::npos);
}

// ---------------------------------------------------------------------------
// 3. XML text "Tom & Jerry <test>" is escaped exactly once in SSML
// ---------------------------------------------------------------------------

TEST(OfflineIntegration, TomAndJerryAmpersandEscapedOnce) {
    Wire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("Tom & Jerry <test>", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    const auto& ssml = ws.sent_messages()[1];
    // Must appear once-escaped: &amp; for &
    EXPECT_NE(ssml.find("&amp;"), std::string::npos);
    // Must NOT appear double-escaped: &amp;amp; would mean the escaper ran twice
    EXPECT_EQ(ssml.find("&amp;amp;"), std::string::npos);
}

TEST(OfflineIntegration, TomAndJerryAngleBracketsEscapedOnce) {
    Wire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("Tom & Jerry <test>", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    const auto& ssml = ws.sent_messages()[1];
    EXPECT_NE(ssml.find("&lt;"), std::string::npos);
    EXPECT_NE(ssml.find("&gt;"), std::string::npos);
    // Raw angle brackets must not appear in the SSML user-text region
    // (they can appear in the surrounding XML tags, which is fine — the
    // TextChunker escapes only the user text before it reaches EdgeProtocol).
    // Double-escaped forms must not appear.
    EXPECT_EQ(ssml.find("&amp;lt;"), std::string::npos);
    EXPECT_EQ(ssml.find("&amp;gt;"), std::string::npos);
}

TEST(OfflineIntegration, TomAndJerryRawAmpersandAbsentFromSsml) {
    Wire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("Tom & Jerry", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    const auto& ssml = ws.sent_messages()[1];
    // The literal " & " (space-ampersand-space) must not appear in the frame.
    EXPECT_EQ(ssml.find(" & "), std::string::npos);
}

// ---------------------------------------------------------------------------
// 4. Unicode text: multi-byte UTF-8 passes verbatim through encoding pipeline
// ---------------------------------------------------------------------------

TEST(OfflineIntegration, JapaneseTextPreservedVerbatimInSsml) {
    Wire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    // "こんにちは世界" — each kana is 3 bytes, each kanji is 3 bytes in UTF-8
    const std::string text =
        "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf"  // こんにちは
        "\xe4\xb8\x96\xe7\x95\x8c";                                       // 世界

    Communicate c(text, TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    EXPECT_NE(ws.sent_messages()[1].find(text), std::string::npos);
}

TEST(OfflineIntegration, ArabicTextPreservedVerbatimInSsml) {
    Wire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    // "مرحبا" — Arabic (each codepoint is 2 bytes in UTF-8)
    const std::string text = "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7";

    Communicate c(text, TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    EXPECT_NE(ws.sent_messages()[1].find(text), std::string::npos);
}

// ---------------------------------------------------------------------------
// 5. Long text: split into ≥2 chunks → 2×(speech.config + ssml) frames sent
// ---------------------------------------------------------------------------

TEST(OfflineIntegration, LongTextSendsTwoSpeechConfigFrames) {
    Wire w;
    FakeWebSocketClient ws;
    // Two sessions — one per chunk.
    push_session(ws, "CHUNK_A");
    push_session(ws, "CHUNK_B");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    std::string long_text(5000, 'x');
    Communicate c(long_text, TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    // 2 chunks × 2 frames = 4 sent messages.
    const auto& msgs = ws.sent_messages();
    ASSERT_TRUE(msgs.size() >= 4u);
    EXPECT_NE(msgs[0].find("Path:speech.config"), std::string::npos);
    EXPECT_NE(msgs[1].find("Path:ssml"), std::string::npos);
    EXPECT_NE(msgs[2].find("Path:speech.config"), std::string::npos);
    EXPECT_NE(msgs[3].find("Path:ssml"), std::string::npos);
}

TEST(OfflineIntegration, LongTextConnectsWebSocketTwice) {
    Wire w;
    FakeWebSocketClient ws;
    push_session(ws, "CHUNK_A");
    push_session(ws, "CHUNK_B");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    std::string long_text(5000, 'x');
    Communicate c(long_text, TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.stream_sync().has_value());

    // SynthesisSession opens one connection per text chunk.
    EXPECT_EQ(ws.connect_count(), 2);
}

// ---------------------------------------------------------------------------
// 6. Multi-chunk offset compensation at the Communicate API level
//
// Reference: communicate.py __compensate_offset():
//   offset_compensation = cumulative_audio_bytes * 8 * 10_000_000 // 48_000
//
// Using N = 6000 audio bytes in chunk 1:
//   compensation = 6000 * 8 * 10_000_000 / 48_000 = 10_000_000 ticks (1 s)
//
// A boundary at raw offset 0 in chunk 2 must arrive at the Communicate API
// with offset_ticks == 10_000_000.
// ---------------------------------------------------------------------------

TEST(OfflineIntegration, MultiChunkOffsetCompensatedInStreamSync) {
    constexpr std::size_t N = 6000;
    constexpr std::int64_t expected_comp =
        static_cast<std::int64_t>(N) * 8LL * 10'000'000LL / 48'000LL;  // = 10_000_000

    Wire w;
    FakeWebSocketClient ws;
    // Chunk 1: 6000 audio bytes + turn.end (no boundary).
    ws.push_incoming(make_audio_frame_bytes(N));
    ws.push_incoming(make_turn_end());
    // Chunk 2: audio + boundary at raw offset 0 + turn.end.
    ws.push_incoming(make_audio_frame("X"));
    ws.push_incoming(make_word_boundary(0, 500'000, "second"));
    ws.push_incoming(make_turn_end());

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    std::string long_text(5000, 'x');
    Communicate c(long_text, TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    auto result = c.stream_sync();
    ASSERT_TRUE(result.has_value());

    // Locate the BoundaryChunk that came from chunk 2 (the only one).
    std::int64_t found_offset = -1;
    for (const auto& chunk : *result) {
        if (std::holds_alternative<BoundaryChunk>(chunk)) {
            found_offset = std::get<BoundaryChunk>(chunk).offset_ticks;
            break;
        }
    }
    EXPECT_EQ(found_offset, expected_comp);
}

TEST(OfflineIntegration, MultiChunkOffsetCompensatedInSrtTimestamp) {
    // Same scenario but through save()/SRT generation.
    // 10_000_000 ticks = 1 second → SRT timestamp contains "00:00:01".
    constexpr std::size_t N = 6000;

    Wire w;
    FakeWebSocketClient ws;
    ws.push_incoming(make_audio_frame_bytes(N));
    ws.push_incoming(make_turn_end());
    ws.push_incoming(make_audio_frame("X"));
    ws.push_incoming(make_word_boundary(0, 500'000, "second"));
    ws.push_incoming(make_turn_end());

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    TempFile mp3{"comp_mp3", ".mp3"};
    TempFile srt{"comp_srt", ".srt"};

    std::string long_text(5000, 'x');
    Communicate c(long_text, TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.save(mp3.path, srt.path).has_value());

    const std::string content = read_file(srt.path);
    // The compensated boundary starts at 10_000_000 ticks = 1.0 s.
    EXPECT_NE(content.find("00:00:01"), std::string::npos);
}

TEST(OfflineIntegration, FirstChunkBoundaryOffsetUnchanged) {
    // Boundaries in the first chunk have compensation = 0.
    Wire w;
    FakeWebSocketClient ws;
    ws.push_incoming(make_word_boundary(1'234'567, 500'000, "word"));
    ws.push_incoming(make_audio_frame("X"));
    ws.push_incoming(make_turn_end());

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    auto result = c.stream_sync();
    ASSERT_TRUE(result.has_value());

    for (const auto& chunk : *result) {
        if (std::holds_alternative<BoundaryChunk>(chunk)) {
            EXPECT_EQ(std::get<BoundaryChunk>(chunk).offset_ticks, 1'234'567);
        }
    }
}

// ---------------------------------------------------------------------------
// 7. Protocol error from fake server propagates to Communicate caller
// ---------------------------------------------------------------------------

TEST(OfflineIntegration, UnknownPathFrameReturnsProtocolError) {
    // The fake server sends a text frame with an unknown Path — EdgeProtocol
    // must reject it with protocol_error, which bubbles up through
    // SynthesisSession to Communicate::stream_sync().
    Wire w;
    FakeWebSocketClient ws;
    ws.push_incoming(make_audio_frame("SOME_AUDIO"));
    ws.push_incoming(make_unknown_path_frame());
    ws.push_incoming(make_turn_end());

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    auto result = c.stream_sync();

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::protocol_error);
}

TEST(OfflineIntegration, ReceiveErrorPropagatesAsNetworkError) {
    Wire w;
    FakeWebSocketClient ws;
    // Inject a receive error — simulates a mid-session connection drop.
    ws.set_receive_error(Error{ErrorCode::network_error, "simulated drop"});

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    auto result = c.stream_sync();

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::network_error);
}

TEST(OfflineIntegration, ProtocolErrorPropagatesFromSave) {
    Wire w;
    FakeWebSocketClient ws;
    ws.push_incoming(make_audio_frame("SOME_AUDIO"));
    ws.push_incoming(make_unknown_path_frame());
    ws.push_incoming(make_turn_end());

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    TempFile mp3{"proto_err", ".mp3"};
    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    auto result = c.save(mp3.path);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::protocol_error);
}

// ---------------------------------------------------------------------------
// 8. No-audio response returns a clear service_error
// ---------------------------------------------------------------------------

TEST(OfflineIntegration, NoAudioResponseReturnsServiceError) {
    // The fake server returns turn.end without sending any audio frame.
    // Reference: communicate.py "if not audio_was_received: raise NoAudioReceived"
    Wire w;
    FakeWebSocketClient ws;
    ws.push_incoming(make_turn_end());

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    auto result = c.stream_sync();

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::service_error);
}

TEST(OfflineIntegration, NoAudioResponseFromSaveReturnsServiceError) {
    Wire w;
    FakeWebSocketClient ws;
    ws.push_incoming(make_turn_end());

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    TempFile mp3{"no_audio", ".mp3"};
    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    auto result = c.save(mp3.path);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::service_error);
}

TEST(OfflineIntegration, NoAudioErrorMessageMentionsAudio) {
    Wire w;
    FakeWebSocketClient ws;
    ws.push_incoming(make_turn_end());

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    Communicate c("hello", TtsConfig::defaults(), CommunicateOptions{},
                  make_seam(session));
    auto result = c.stream_sync();

    ASSERT_FALSE(result.has_value());
    // Error message must mention "audio" (reference: "No audio was received").
    const std::string msg = result.error().what();
    EXPECT_NE(msg.find("audio"), std::string::npos);
}
