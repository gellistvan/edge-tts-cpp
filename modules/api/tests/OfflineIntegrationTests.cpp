// Offline end-to-end integration tests: SpeechSynthesizer → SynthesisSession →
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
//   - Multi-chunk offset compensation at the SpeechSynthesizer level (the second
//     chunk's word-boundary ticks are shifted by the audio duration of the
//     first chunk).
//   - Error propagation: protocol errors and no-audio responses from the
//     fake server surface as the correct ErrorCode at the SpeechSynthesizer API.
//
// All tests use FakeWebSocketClient — no real network, no live service.

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

namespace fs = std::filesystem;

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
using edge_tts::common::Error;
using edge_tts::common::ErrorCode;
using edge_tts::common::IdGenerator;
using edge_tts::common::SystemClock;
using edge_tts::core::AudioChunk;
using edge_tts::core::BoundaryChunk;
using edge_tts::core::TtsChunk;
using edge_tts::core::TtsConfig;
using edge_tts::test::make_seam;
using edge_tts::test::push_session;
using edge_tts::test::read_file;
using edge_tts::test::TestWire;

// ---------------------------------------------------------------------------
// Shared fixtures
// ---------------------------------------------------------------------------

namespace {

using edge_tts::test::make_audio_frame;
using edge_tts::test::make_turn_end;
using edge_tts::test::make_word_boundary;

// A text frame with an unknown Path value — parse_incoming must reject it.
static WebSocketMessage make_unknown_path_frame() {
    WebSocketMessage m;
    m.type = WebSocketMessage::Type::text;
    m.text = "X-RequestId:abc\r\nPath:not.a.real.path\r\n\r\n{}";
    return m;
}

// RAII temp-file.
struct TempFile {
    fs::path path;
    explicit TempFile(const std::string& tag, const std::string& ext)
        : path(fs::temp_directory_path() / ("offline_int_" + tag + ext)) {}
    ~TempFile() { std::error_code ec; fs::remove(path, ec); }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// 1. Short text produces exactly one audio chunk
// ---------------------------------------------------------------------------

TEST(OfflineIntegration, ShortTextProducesOneAudioChunk) {
    TestWire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIODATA");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer c("hello", TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    auto result = c.synthesize();

    ASSERT_TRUE(result.has_value());
    int audio_count = 0;
    for (const auto& chunk : *result)
        if (std::holds_alternative<AudioChunk>(chunk)) ++audio_count;
    EXPECT_EQ(audio_count, 1);
}

TEST(OfflineIntegration, ShortTextAudioBytesMatchFakePayload) {
    TestWire w;
    FakeWebSocketClient ws;
    push_session(ws, "HELLO_BYTES");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer c("hello", TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    auto result = c.synthesize();

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
    TestWire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer c("hello", TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.synthesize().has_value());

    const auto& msgs = ws.sent_messages();
    // Two frames per chunk: speech.config at index 0, ssml at index 1.
    ASSERT_TRUE(msgs.size() >= 2u);
    EXPECT_NE(msgs[0].find("Path:speech.config"), std::string::npos);
}

TEST(OfflineIntegration, SpeechConfigFrameHasContentTypeJson) {
    TestWire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer c("hello", TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.synthesize().has_value());

    const auto& msgs = ws.sent_messages();
    ASSERT_TRUE(msgs.size() >= 1u);
    EXPECT_NE(msgs[0].find("Content-Type:application/json"), std::string::npos);
}

TEST(OfflineIntegration, SsmlFrameHasSsmlPath) {
    TestWire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer c("hello", TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.synthesize().has_value());

    const auto& msgs = ws.sent_messages();
    ASSERT_TRUE(msgs.size() >= 2u);
    EXPECT_NE(msgs[1].find("Path:ssml"), std::string::npos);
}

TEST(OfflineIntegration, SsmlFrameHasXRequestIdHeader) {
    TestWire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer c("hello", TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.synthesize().has_value());

    const auto& msgs = ws.sent_messages();
    ASSERT_TRUE(msgs.size() >= 2u);
    EXPECT_NE(msgs[1].find("X-RequestId:"), std::string::npos);
}

TEST(OfflineIntegration, SsmlFrameRequestIdIs32CharHex) {
    TestWire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer c("hello", TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.synthesize().has_value());

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
    TestWire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer c("hello", TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.synthesize().has_value());

    const auto& ssml = ws.sent_messages()[1];
    EXPECT_NE(ssml.find("<speak"), std::string::npos);
    EXPECT_NE(ssml.find("</speak>"), std::string::npos);
}

// ---------------------------------------------------------------------------
// 3. XML text "Tom & Jerry <test>" is escaped exactly once in SSML
// ---------------------------------------------------------------------------

TEST(OfflineIntegration, TomAndJerryAmpersandEscapedOnce) {
    TestWire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer c("Tom & Jerry <test>", TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.synthesize().has_value());

    const auto& ssml = ws.sent_messages()[1];
    // Must appear once-escaped: &amp; for &
    EXPECT_NE(ssml.find("&amp;"), std::string::npos);
    // Must NOT appear double-escaped: &amp;amp; would mean the escaper ran twice
    EXPECT_EQ(ssml.find("&amp;amp;"), std::string::npos);
}

TEST(OfflineIntegration, TomAndJerryAngleBracketsEscapedOnce) {
    TestWire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer c("Tom & Jerry <test>", TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.synthesize().has_value());

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
    TestWire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer c("Tom & Jerry", TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.synthesize().has_value());

    const auto& ssml = ws.sent_messages()[1];
    // The literal " & " (space-ampersand-space) must not appear in the frame.
    EXPECT_EQ(ssml.find(" & "), std::string::npos);
}

// ---------------------------------------------------------------------------
// 4. Unicode text: multi-byte UTF-8 passes verbatim through encoding pipeline
// ---------------------------------------------------------------------------

TEST(OfflineIntegration, JapaneseTextPreservedVerbatimInSsml) {
    TestWire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    // "こんにちは世界" — each kana is 3 bytes, each kanji is 3 bytes in UTF-8
    const std::string text =
        "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf"  // こんにちは
        "\xe4\xb8\x96\xe7\x95\x8c";                                       // 世界

    SpeechSynthesizer c(text, TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.synthesize().has_value());

    EXPECT_NE(ws.sent_messages()[1].find(text), std::string::npos);
}

TEST(OfflineIntegration, ArabicTextPreservedVerbatimInSsml) {
    TestWire w;
    FakeWebSocketClient ws;
    push_session(ws, "AUDIO");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    // "مرحبا" — Arabic (each codepoint is 2 bytes in UTF-8)
    const std::string text = "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7";

    SpeechSynthesizer c(text, TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.synthesize().has_value());

    EXPECT_NE(ws.sent_messages()[1].find(text), std::string::npos);
}

// ---------------------------------------------------------------------------
// 5. Long text: split into ≥2 chunks → 2×(speech.config + ssml) frames sent
// ---------------------------------------------------------------------------

TEST(OfflineIntegration, LongTextSendsTwoSpeechConfigFrames) {
    TestWire w;
    FakeWebSocketClient ws;
    // Two sessions — one per chunk.
    push_session(ws, "CHUNK_A");
    push_session(ws, "CHUNK_B");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    std::string long_text(5000, 'x');
    SpeechSynthesizer c(long_text, TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.synthesize().has_value());

    // 2 chunks × 2 frames = 4 sent messages.
    const auto& msgs = ws.sent_messages();
    ASSERT_TRUE(msgs.size() >= 4u);
    EXPECT_NE(msgs[0].find("Path:speech.config"), std::string::npos);
    EXPECT_NE(msgs[1].find("Path:ssml"), std::string::npos);
    EXPECT_NE(msgs[2].find("Path:speech.config"), std::string::npos);
    EXPECT_NE(msgs[3].find("Path:ssml"), std::string::npos);
}

TEST(OfflineIntegration, LongTextConnectsWebSocketTwice) {
    TestWire w;
    FakeWebSocketClient ws;
    push_session(ws, "CHUNK_A");
    push_session(ws, "CHUNK_B");
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    std::string long_text(5000, 'x');
    SpeechSynthesizer c(long_text, TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.synthesize().has_value());

    // SynthesisSession opens one connection per text chunk.
    EXPECT_EQ(ws.connect_count(), 2);
}

// ---------------------------------------------------------------------------
// 6. Multi-chunk offset compensation at the SpeechSynthesizer API level
//
// The service reports boundary offsets relative to each chunk's audio start.
// SynthesisSession converts them to absolute offsets by accumulating the
// max(offset_ticks + duration_ticks) from each completed chunk's boundaries.
// Audio byte counts play no role.
//
// Chunk 1: boundary at offset=5_000_000, duration=5_000_000 → end=10_000_000
// Chunk 2: boundary at raw offset 0 → global offset = 0 + 10_000_000
// ---------------------------------------------------------------------------

TEST(OfflineIntegration, MultiChunkOffsetCompensatedInSynthesize) {
    // Chunk 1 has a boundary at 500ms (5_000_000 ticks), duration 500ms
    // → end = 10_000_000 ticks = compensation for chunk 2.
    constexpr std::int64_t chunk1_offset   = 5'000'000;
    constexpr std::int64_t chunk1_duration = 5'000'000;
    constexpr std::int64_t expected_comp   = chunk1_offset + chunk1_duration;  // 10_000_000

    TestWire w;
    FakeWebSocketClient ws;
    // Chunk 1: boundary that drives compensation + audio + turn.end
    ws.push_incoming(make_word_boundary(chunk1_offset, chunk1_duration, "first"));
    ws.push_incoming(make_audio_frame("AUDIO1"));
    ws.push_incoming(make_turn_end());
    // Chunk 2: audio + boundary at raw offset 0 + turn.end
    ws.push_incoming(make_audio_frame("AUDIO2"));
    ws.push_incoming(make_word_boundary(0, 500'000, "second"));
    ws.push_incoming(make_turn_end());

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    std::string long_text(5000, 'x');
    SpeechSynthesizer c(long_text, TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    auto result = c.synthesize();
    ASSERT_TRUE(result.has_value());

    // The second boundary (from chunk 2) must be shifted by expected_comp.
    std::int64_t found_offset = -1;
    int boundary_count = 0;
    for (const auto& chunk : *result) {
        if (std::holds_alternative<BoundaryChunk>(chunk)) {
            ++boundary_count;
            if (boundary_count == 2)
                found_offset = std::get<BoundaryChunk>(chunk).offset_ticks;
        }
    }
    EXPECT_EQ(boundary_count, 2);
    EXPECT_EQ(found_offset, expected_comp);
}

TEST(OfflineIntegration, MultiChunkOffsetCompensatedInSrtTimestamp) {
    // Same scenario through save()/SRT generation.
    // Chunk 2 boundary gets compensation = 10_000_000 ticks = 1.0 s.
    // SRT timestamp must contain "00:00:01".
    constexpr std::int64_t chunk1_offset   = 5'000'000;
    constexpr std::int64_t chunk1_duration = 5'000'000;

    TestWire w;
    FakeWebSocketClient ws;
    ws.push_incoming(make_word_boundary(chunk1_offset, chunk1_duration, "first"));
    ws.push_incoming(make_audio_frame("AUDIO1"));
    ws.push_incoming(make_turn_end());
    ws.push_incoming(make_audio_frame("AUDIO2"));
    ws.push_incoming(make_word_boundary(0, 500'000, "second"));
    ws.push_incoming(make_turn_end());

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    TempFile mp3{"comp_mp3", ".mp3"};
    TempFile srt{"comp_srt", ".srt"};

    std::string long_text(5000, 'x');
    SpeechSynthesizer c(long_text, TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    ASSERT_TRUE(c.save(mp3.path, srt.path).has_value());

    const std::string content = read_file(srt.path);
    // Compensated boundary starts at 10_000_000 ticks = 1.0 s.
    EXPECT_NE(content.find("00:00:01"), std::string::npos);
}

TEST(OfflineIntegration, MultiChunkAudioSizeDoesNotAffectSubtitleTimestamp) {
    // Regression: subtitle timestamps must be identical regardless of audio
    // byte count.  Run two sessions with different audio sizes but identical
    // boundary metadata and assert they produce the same SRT output.
    constexpr std::int64_t chunk1_offset   = 5'000'000;
    constexpr std::int64_t chunk1_duration = 5'000'000;

    auto run = [&](const std::string& audio_payload) -> std::string {
        TestWire w;
        FakeWebSocketClient ws;
        ws.push_incoming(make_word_boundary(chunk1_offset, chunk1_duration, "first"));
        ws.push_incoming(make_audio_frame(audio_payload));
        ws.push_incoming(make_turn_end());
        ws.push_incoming(make_audio_frame("X"));
        ws.push_incoming(make_word_boundary(0, 500'000, "second"));
        ws.push_incoming(make_turn_end());

        SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};
        TempFile mp3{"audiosize_mp3", ".mp3"};
        TempFile srt{"audiosize_srt", ".srt"};
        std::string long_text(5000, 'x');
        SpeechSynthesizer c(long_text, TtsConfig::defaults(), SynthesisOptions{},
                      make_seam(session));
        (void)c.save(mp3.path, srt.path);
        return read_file(srt.path);
    };

    const std::string srt_small = run(std::string(10,   'A'));
    const std::string srt_large = run(std::string(6000, 'A'));

    EXPECT_EQ(srt_small, srt_large);
    EXPECT_NE(srt_small.find("00:00:01"), std::string::npos);
}

TEST(OfflineIntegration, FirstChunkBoundaryOffsetUnchanged) {
    // Boundaries in the first chunk have compensation = 0.
    TestWire w;
    FakeWebSocketClient ws;
    ws.push_incoming(make_word_boundary(1'234'567, 500'000, "word"));
    ws.push_incoming(make_audio_frame("X"));
    ws.push_incoming(make_turn_end());

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer c("hello", TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    auto result = c.synthesize();
    ASSERT_TRUE(result.has_value());

    for (const auto& chunk : *result) {
        if (std::holds_alternative<BoundaryChunk>(chunk)) {
            EXPECT_EQ(std::get<BoundaryChunk>(chunk).offset_ticks, 1'234'567);
        }
    }
}

// ---------------------------------------------------------------------------
// 7. Protocol error from fake server propagates to SpeechSynthesizer caller
// ---------------------------------------------------------------------------

TEST(OfflineIntegration, UnknownPathFrameReturnsProtocolError) {
    // The fake server sends a text frame with an unknown Path — EdgeProtocol
    // must reject it with protocol_error, which bubbles up through
    // SynthesisSession to SpeechSynthesizer::synthesize()().
    TestWire w;
    FakeWebSocketClient ws;
    ws.push_incoming(make_audio_frame("SOME_AUDIO"));
    ws.push_incoming(make_unknown_path_frame());
    ws.push_incoming(make_turn_end());

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer c("hello", TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    auto result = c.synthesize();

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::protocol_error);
}

TEST(OfflineIntegration, ReceiveErrorPropagatesAsNetworkError) {
    TestWire w;
    FakeWebSocketClient ws;
    // Inject a receive error — simulates a mid-session connection drop.
    ws.set_receive_error(Error{ErrorCode::network_error, "simulated drop"});

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer c("hello", TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    auto result = c.synthesize();

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::network_error);
}

TEST(OfflineIntegration, ProtocolErrorPropagatesFromSave) {
    TestWire w;
    FakeWebSocketClient ws;
    ws.push_incoming(make_audio_frame("SOME_AUDIO"));
    ws.push_incoming(make_unknown_path_frame());
    ws.push_incoming(make_turn_end());

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    TempFile mp3{"proto_err", ".mp3"};
    SpeechSynthesizer c("hello", TtsConfig::defaults(), SynthesisOptions{},
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
    TestWire w;
    FakeWebSocketClient ws;
    ws.push_incoming(make_turn_end());

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer c("hello", TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    auto result = c.synthesize();

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::service_error);
}

TEST(OfflineIntegration, NoAudioResponseFromSaveReturnsServiceError) {
    TestWire w;
    FakeWebSocketClient ws;
    ws.push_incoming(make_turn_end());

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    TempFile mp3{"no_audio", ".mp3"};
    SpeechSynthesizer c("hello", TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    auto result = c.save(mp3.path);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::service_error);
}

TEST(OfflineIntegration, NoAudioErrorMessageMentionsAudio) {
    TestWire w;
    FakeWebSocketClient ws;
    ws.push_incoming(make_turn_end());

    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer c("hello", TtsConfig::defaults(), SynthesisOptions{},
                  make_seam(session));
    auto result = c.synthesize();

    ASSERT_FALSE(result.has_value());
    // Error message must mention "audio" (reference: "No audio was received").
    const std::string msg = result.error().what();
    EXPECT_NE(msg.find("audio"), std::string::npos);
}
