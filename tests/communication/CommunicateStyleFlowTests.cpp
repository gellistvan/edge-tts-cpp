// Communication-level integration tests.
//
// These tests verify a "Communicate-style flow" — the same sequence of operations
// that api::Communicate uses in production — entirely within the communication
// layer using FakeWebSocketClient.  No real network calls are made.
//
// Flow mirrors api::Communicate::run_synthesis():
//   1. TTS config validation (done by Communicate before calling synthesizer)
//   2. Text already chunked + XML-escaped (by TextChunker)
//   3. SynthesisSession::synthesize(config, chunks) → audio + boundary chunks

#include "edge_tts/communication/SynthesisSession.hpp"
#include "edge_tts/communication/ConnectionMetadata.hpp"
#include "edge_tts/communication/EdgeProtocol.hpp"
#include "edge_tts/communication/EdgeServiceConfig.hpp"
#include "edge_tts/communication/EdgeTokenProvider.hpp"
#include "edge_tts/communication/FakeWebSocketClient.hpp"
#include "edge_tts/communication/WebSocketMessage.hpp"
#include "edge_tts/common/Clock.hpp"
#include "edge_tts/common/IdGenerator.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/serialization/TextChunker.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

using edge_tts::communication::ConnectionMetadataFactory;
using edge_tts::communication::EdgeProtocol;
using edge_tts::communication::EdgeServiceConfig;
using edge_tts::communication::EdgeTokenProvider;
using edge_tts::communication::FakeWebSocketClient;
using edge_tts::communication::SynthesisSession;
using edge_tts::communication::WebSocketMessage;
using edge_tts::common::IdGenerator;
using edge_tts::common::SystemClock;
using edge_tts::core::AudioChunk;
using edge_tts::core::BoundaryChunk;
using edge_tts::core::TtsChunk;
using edge_tts::core::TtsConfig;
using edge_tts::serialization::TextChunker;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::vector<std::byte> to_bytes(const std::string& s) {
    std::vector<std::byte> v;
    for (char c : s) v.push_back(static_cast<std::byte>(c));
    return v;
}

static WebSocketMessage make_audio_frame(const std::vector<std::byte>& body) {
    const std::string hdr = "X-RequestId:abc\r\nPath:audio\r\nContent-Type:audio/mpeg";
    const auto hl = static_cast<uint16_t>(2 + hdr.size());
    std::vector<std::byte> frame;
    frame.reserve(2 + hdr.size() + 2 + body.size());
    frame.push_back(static_cast<std::byte>(hl >> 8));
    frame.push_back(static_cast<std::byte>(hl & 0xff));
    for (char c : hdr)  frame.push_back(static_cast<std::byte>(c));
    frame.push_back(static_cast<std::byte>('\r'));
    frame.push_back(static_cast<std::byte>('\n'));
    for (auto b : body) frame.push_back(b);
    WebSocketMessage m;
    m.type   = WebSocketMessage::Type::binary;
    m.binary = std::move(frame);
    return m;
}

static WebSocketMessage make_turn_end() {
    WebSocketMessage m;
    m.type = WebSocketMessage::Type::text;
    m.text = "X-RequestId:abc\r\nPath:turn.end\r\n\r\n";
    return m;
}

static WebSocketMessage make_word_boundary(int64_t offset, int64_t duration,
                                            const std::string& word) {
    WebSocketMessage m;
    m.type = WebSocketMessage::Type::text;
    m.text = "X-RequestId:abc\r\nPath:audio.metadata\r\n\r\n"
             "{\"Metadata\":[{\"Type\":\"WordBoundary\","
             "\"Data\":{\"Offset\":" + std::to_string(offset) +
             ",\"Duration\":" + std::to_string(duration) +
             ",\"text\":{\"Text\":\"" + word + "\"}}}]}";
    return m;
}

// ---------------------------------------------------------------------------
// Fixture helpers
// ---------------------------------------------------------------------------

struct SessionFixture {
    SystemClock                clock;
    IdGenerator                ids;
    EdgeServiceConfig          svc;
    EdgeTokenProvider          token_provider;
    EdgeProtocol               protocol;
    ConnectionMetadataFactory  meta_factory;

    explicit SessionFixture()
        : svc{edge_tts::communication::default_edge_service_config()}
        , token_provider{svc, clock}
        , protocol{clock}
        , meta_factory{ids}
    {}

    SynthesisSession make_session(FakeWebSocketClient& ws) {
        return SynthesisSession{ws, protocol, svc, token_provider, meta_factory, clock};
    }
};

static TtsConfig valid_config() { return TtsConfig::defaults(); }

// ---------------------------------------------------------------------------
// Full Communicate-style flow: text → chunk → session → audio
// ---------------------------------------------------------------------------

TEST(CommunicateStyleFlow, SingleChunkTextProducesAudio) {
    SessionFixture fix;
    FakeWebSocketClient fake_ws;
    fake_ws.push_incoming(make_audio_frame(to_bytes("MP3DATA")));
    fake_ws.push_incoming(make_turn_end());

    auto session = fix.make_session(fake_ws);

    // Simulate what api::Communicate does: chunk + escape the input text.
    TextChunker chunker;
    auto chunks = chunker.chunk("Hello world.");
    ASSERT_TRUE(chunks.has_value());
    ASSERT_FALSE(chunks->empty());

    auto result = session.synthesize(valid_config(), *chunks);

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->empty());
    EXPECT_TRUE(edge_tts::core::is_audio((*result)[0]));
    const auto& ac = std::get<AudioChunk>((*result)[0]);
    const std::string got(reinterpret_cast<const char*>(ac.data.data()), ac.data.size());
    EXPECT_EQ(got, "MP3DATA");
}

TEST(CommunicateStyleFlow, TextWithXmlSpecialCharsProducesAudio) {
    // TextChunker XML-escapes special characters. SynthesisSession must pass
    // the pre-escaped chunk verbatim without re-escaping.
    SessionFixture fix;
    FakeWebSocketClient fake_ws;
    fake_ws.push_incoming(make_audio_frame(to_bytes("AUDIO")));
    fake_ws.push_incoming(make_turn_end());

    auto session = fix.make_session(fake_ws);

    TextChunker chunker;
    auto chunks = chunker.chunk("Tom & Jerry <show>");
    ASSERT_TRUE(chunks.has_value());
    ASSERT_FALSE(chunks->empty());

    // The chunk must be XML-escaped by TextChunker.
    EXPECT_NE(chunks->front().find("&amp;"), std::string::npos);
    EXPECT_NE(chunks->front().find("&lt;"), std::string::npos);

    auto result = session.synthesize(valid_config(), *chunks);
    ASSERT_TRUE(result.has_value());

    // Verify the SSML frame sent to the fake WS contains &amp; (not double-escaped &amp;amp;).
    ASSERT_TRUE(fake_ws.sent_messages().size() >= 2);
    const std::string& ssml_frame = fake_ws.sent_messages()[1]; // [0] = speech.config
    EXPECT_NE(ssml_frame.find("&amp;"), std::string::npos);
    EXPECT_EQ(ssml_frame.find("&amp;amp;"), std::string::npos);
}

TEST(CommunicateStyleFlow, AudioAndBoundaryChunksReturnedInOrder) {
    // Simulate a session that returns a word boundary followed by audio.
    SessionFixture fix;
    FakeWebSocketClient fake_ws;
    fake_ws.push_incoming(make_word_boundary(0, 5'000'000, "Hello"));
    fake_ws.push_incoming(make_audio_frame(to_bytes("AUDIO_DATA")));
    fake_ws.push_incoming(make_turn_end());

    auto session = fix.make_session(fake_ws);

    TextChunker chunker;
    auto chunks = chunker.chunk("Hello.");
    ASSERT_TRUE(chunks.has_value());

    auto result = session.synthesize(valid_config(), *chunks);
    ASSERT_TRUE(result.has_value());

    // Both a boundary and an audio chunk must be present.
    bool has_audio    = false;
    bool has_boundary = false;
    for (const auto& c : *result) {
        if (edge_tts::core::is_audio(c))    has_audio    = true;
        if (edge_tts::core::is_boundary(c)) has_boundary = true;
    }
    EXPECT_TRUE(has_audio);
    EXPECT_TRUE(has_boundary);
}

TEST(CommunicateStyleFlow, MultipleChunksEachProduceAudio) {
    // Long text → TextChunker produces 2+ chunks → SynthesisSession opens a
    // new WebSocket connection per chunk.  Queue enough sessions.
    SessionFixture fix;
    FakeWebSocketClient fake_ws;

    // Two sessions for two chunks.
    fake_ws.push_incoming(make_audio_frame(to_bytes("AUDIO_1")));
    fake_ws.push_incoming(make_turn_end());
    fake_ws.push_incoming(make_audio_frame(to_bytes("AUDIO_2")));
    fake_ws.push_incoming(make_turn_end());

    auto session = fix.make_session(fake_ws);

    std::string long_text(5000, 'A');
    TextChunker chunker;
    auto chunks = chunker.chunk(long_text);
    ASSERT_TRUE(chunks.has_value());
    ASSERT_TRUE(chunks->size() >= 2u);

    auto result = session.synthesize(valid_config(), *chunks);
    ASSERT_TRUE(result.has_value());

    int audio_count = 0;
    for (const auto& c : *result)
        if (edge_tts::core::is_audio(c)) ++audio_count;
    EXPECT_EQ(audio_count, 2);
}

TEST(CommunicateStyleFlow, EmptyTextProducesNoChunksNoSession) {
    // api::Communicate returns early for empty text before calling the synthesizer.
    // At the communication level, if synthesize() is called with zero chunks,
    // it should return an empty result without touching the WebSocket.
    SessionFixture fix;
    FakeWebSocketClient fake_ws; // no messages queued

    auto session = fix.make_session(fake_ws);

    std::vector<std::string> no_chunks;
    auto result = session.synthesize(valid_config(),
        std::span<const std::string>{no_chunks});

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
    EXPECT_EQ(fake_ws.connect_count(), 0);
}
