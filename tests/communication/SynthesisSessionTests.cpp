#include "edge_tts/communication/SynthesisSession.hpp"
#include "edge_tts/communication/FakeWebSocketClient.hpp"
#include "edge_tts/communication/EdgeProtocol.hpp"
#include "edge_tts/communication/EdgeServiceConfig.hpp"
#include "edge_tts/communication/EdgeTokenProvider.hpp"
#include "edge_tts/communication/ConnectionMetadata.hpp"
#include "edge_tts/common/Clock.hpp"
#include "edge_tts/common/Error.hpp"
#include "edge_tts/common/IdGenerator.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
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
using edge_tts::common::Error;
using edge_tts::common::ErrorCode;
using edge_tts::common::FixedClock;
using edge_tts::common::IdGenerator;
using edge_tts::core::AudioChunk;
using edge_tts::core::BoundaryChunk;
using edge_tts::core::TtsChunk;
using edge_tts::core::TtsConfig;

// ---------------------------------------------------------------------------
// Shared fixtures
// ---------------------------------------------------------------------------

static FixedClock g_clock{
    std::chrono::system_clock::time_point{std::chrono::seconds{1705314645LL}}};

static EdgeProtocol& get_protocol() {
    static EdgeProtocol proto{g_clock};
    return proto;
}

static EdgeServiceConfig make_test_config() {
    auto cfg = edge_tts::communication::default_edge_service_config();
    return cfg;
}

static EdgeTokenProvider& get_token_provider() {
    static EdgeTokenProvider tp{make_test_config(), g_clock};
    return tp;
}

static IdGenerator& get_ids() {
    static IdGenerator ids;
    return ids;
}

static ConnectionMetadataFactory& get_meta_factory() {
    static ConnectionMetadataFactory factory{get_ids()};
    return factory;
}

static SynthesisSession make_session(FakeWebSocketClient& fake) {
    return SynthesisSession{
        fake,
        get_protocol(),
        make_test_config(),
        get_token_provider(),
        get_meta_factory()
    };
}

// ---------------------------------------------------------------------------
// Frame builders for the fake incoming queue
// ---------------------------------------------------------------------------

// Build a well-formed binary audio frame.
// See EdgeProtocolIncomingTests.cpp for format details.
static WebSocketMessage make_audio_frame(const std::vector<std::byte>& body) {
    const std::string hdr = "X-RequestId:abc\r\nPath:audio\r\nContent-Type:audio/mpeg";
    const auto hl = static_cast<uint16_t>(2 + hdr.size());
    std::vector<std::byte> frame;
    frame.reserve(2 + hdr.size() + 2 + body.size());
    frame.push_back(static_cast<std::byte>(hl >> 8));
    frame.push_back(static_cast<std::byte>(hl & 0xff));
    for (char c : hdr) frame.push_back(static_cast<std::byte>(c));
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
                                            const std::string& text) {
    WebSocketMessage m;
    m.type = WebSocketMessage::Type::text;
    m.text = "X-RequestId:abc\r\nPath:audio.metadata\r\n\r\n"
             "{\"Metadata\":[{\"Type\":\"WordBoundary\","
             "\"Data\":{\"Offset\":" + std::to_string(offset) +
             ",\"Duration\":" + std::to_string(duration) +
             ",\"text\":{\"Text\":\"" + text + "\"}}}]}";
    return m;
}

static std::vector<std::byte> audio_bytes(const std::string& s) {
    std::vector<std::byte> v;
    for (char c : s) v.push_back(static_cast<std::byte>(c));
    return v;
}

// Push a complete minimal chunk sequence: audio + turn.end
static void push_minimal_chunk(FakeWebSocketClient& fake) {
    fake.push_incoming(make_audio_frame(audio_bytes("MP3DATA")));
    fake.push_incoming(make_turn_end());
}

// ---------------------------------------------------------------------------
// Connects once per chunk
// ---------------------------------------------------------------------------

TEST(SynthesisSession, ConnectsOnceForOneChunk) {
    FakeWebSocketClient fake;
    push_minimal_chunk(fake);
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello world"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(fake.connect_count(), 1);
}

TEST(SynthesisSession, ConnectsTwiceForTwoChunks) {
    FakeWebSocketClient fake;
    push_minimal_chunk(fake);  // for chunk 1
    push_minimal_chunk(fake);  // for chunk 2
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello", "world"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(fake.connect_count(), 2);
}

// ---------------------------------------------------------------------------
// Sends speech.config before SSML for each chunk
// Reference: send_command_request() then send_ssml_request()
// ---------------------------------------------------------------------------

TEST(SynthesisSession, SendsSpeechConfigBeforeSsml) {
    FakeWebSocketClient fake;
    push_minimal_chunk(fake);
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());

    // Two sends: speech.config (Path:speech.config) then SSML (Path:ssml)
    EXPECT_EQ(fake.send_count(), 2);
    EXPECT_NE(fake.sent_messages()[0].find("speech.config"), std::string::npos);
    EXPECT_NE(fake.sent_messages()[1].find("Path:ssml"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Sends one SSML for one chunk
// ---------------------------------------------------------------------------

TEST(SynthesisSession, SendsOneRoundOfFramesForOneChunk) {
    FakeWebSocketClient fake;
    push_minimal_chunk(fake);
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello world"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(fake.send_count(), 2);  // 1 speech.config + 1 SSML
}

// ---------------------------------------------------------------------------
// Sends multiple SSML frames for multiple chunks (one per chunk)
// ---------------------------------------------------------------------------

TEST(SynthesisSession, SendsTwoSsmlFramesForTwoChunks) {
    FakeWebSocketClient fake;
    push_minimal_chunk(fake);
    push_minimal_chunk(fake);
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"first", "second"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(fake.send_count(), 4);  // 2 speech.config + 2 SSML
}

TEST(SynthesisSession, EachSsmlContainsItsChunkText) {
    FakeWebSocketClient fake;
    push_minimal_chunk(fake);
    push_minimal_chunk(fake);
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"alpha", "beta"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());

    // sent_messages: [speech.config, ssml(alpha), speech.config, ssml(beta)]
    EXPECT_NE(fake.sent_messages()[1].find("alpha"), std::string::npos);
    EXPECT_NE(fake.sent_messages()[3].find("beta"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Yields audio chunks
// ---------------------------------------------------------------------------

TEST(SynthesisSession, YieldsAudioChunk) {
    FakeWebSocketClient fake;
    fake.push_incoming(make_audio_frame(audio_bytes("AUDIODATA")));
    fake.push_incoming(make_turn_end());
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());

    bool has_audio = false;
    for (const auto& c : *result)
        if (std::holds_alternative<AudioChunk>(c)) has_audio = true;
    EXPECT_TRUE(has_audio);
}

TEST(SynthesisSession, AudioBytesPreservedExactly) {
    FakeWebSocketClient fake;
    const auto body = audio_bytes("AUDIOBYTES");
    fake.push_incoming(make_audio_frame(body));
    fake.push_incoming(make_turn_end());
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());

    const auto& audio = std::get<AudioChunk>((*result)[0]);
    EXPECT_EQ(audio.data, body);
}

// ---------------------------------------------------------------------------
// Yields boundary chunks
// ---------------------------------------------------------------------------

TEST(SynthesisSession, YieldsBoundaryChunk) {
    FakeWebSocketClient fake;
    fake.push_incoming(make_word_boundary(100, 200, "hello"));
    fake.push_incoming(make_audio_frame(audio_bytes("MP3")));
    fake.push_incoming(make_turn_end());
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"hello"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());

    bool has_boundary = false;
    for (const auto& c : *result)
        if (std::holds_alternative<BoundaryChunk>(c)) has_boundary = true;
    EXPECT_TRUE(has_boundary);
}

TEST(SynthesisSession, BoundaryChunkFieldsArePreserved) {
    FakeWebSocketClient fake;
    fake.push_incoming(make_word_boundary(1000, 2000, "test"));
    fake.push_incoming(make_audio_frame(audio_bytes("MP3")));
    fake.push_incoming(make_turn_end());
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"test"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());

    const auto& bc = std::get<BoundaryChunk>((*result)[0]);
    EXPECT_EQ(bc.text, "test");
    EXPECT_EQ(bc.offset_ticks, 1000);
    EXPECT_EQ(bc.duration_ticks, 2000);
}

// ---------------------------------------------------------------------------
// Stops receiving on turn.end (does not consume beyond turn.end)
// ---------------------------------------------------------------------------

TEST(SynthesisSession, StopsOnTurnEnd) {
    FakeWebSocketClient fake;
    fake.push_incoming(make_audio_frame(audio_bytes("A")));
    fake.push_incoming(make_turn_end());
    // Extra messages that should NOT be consumed
    fake.push_incoming(make_audio_frame(audio_bytes("B")));
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());

    // Only one audio chunk (from before turn.end), "B" was not consumed
    EXPECT_EQ(result->size(), 1u);
    EXPECT_EQ(fake.incoming_queue_size(), 1u);  // "B" still in queue
}

// ---------------------------------------------------------------------------
// Closes on success
// Reference: context manager __aexit__ always closes
// ---------------------------------------------------------------------------

TEST(SynthesisSession, ClosesAfterSuccessfulChunk) {
    FakeWebSocketClient fake;
    push_minimal_chunk(fake);
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(fake.is_closed());
}

TEST(SynthesisSession, ClosesAfterEachChunk) {
    // For 2 chunks, connect/close cycle happens twice.
    // After the session, the fake must be closed.
    FakeWebSocketClient fake;
    push_minimal_chunk(fake);
    push_minimal_chunk(fake);
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"first", "second"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(fake.is_closed());
}

// ---------------------------------------------------------------------------
// Closes on error (reference: context manager always closes)
// ---------------------------------------------------------------------------

TEST(SynthesisSession, ClosesWhenReceiveErrors) {
    FakeWebSocketClient fake;
    // Let receive fail immediately so we observe close-on-error behavior
    fake.set_receive_error(Error{ErrorCode::network_error, "dropped"});
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(fake.is_closed());
}

TEST(SynthesisSession, ClosesWhenSendErrors) {
    FakeWebSocketClient fake;
    fake.set_send_error(Error{ErrorCode::network_error, "send failed"});
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(fake.is_closed());
}

// ---------------------------------------------------------------------------
// Connect error propagates (no close because connect failed)
// Reference: connect failure happens before the context manager enters
// ---------------------------------------------------------------------------

TEST(SynthesisSession, ConnectErrorPropagates) {
    FakeWebSocketClient fake;
    fake.set_connect_error(Error{ErrorCode::network_error, "refused"});
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::network_error);
}

// ---------------------------------------------------------------------------
// Send error propagates
// ---------------------------------------------------------------------------

TEST(SynthesisSession, SendErrorPropagates) {
    FakeWebSocketClient fake;
    fake.set_send_error(Error{ErrorCode::network_error, "send failed"});
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::network_error);
}

// ---------------------------------------------------------------------------
// Receive error propagates
// ---------------------------------------------------------------------------

TEST(SynthesisSession, ReceiveErrorPropagates) {
    FakeWebSocketClient fake;
    fake.set_receive_error(Error{ErrorCode::network_error, "dropped"});
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::network_error);
}

// ---------------------------------------------------------------------------
// Malformed incoming message propagates protocol_error
// ---------------------------------------------------------------------------

TEST(SynthesisSession, MalformedIncomingPropagatesError) {
    FakeWebSocketClient fake;
    WebSocketMessage bad;
    bad.type = WebSocketMessage::Type::text;
    bad.text = "no-separator-in-this-text-frame";
    fake.push_incoming(bad);
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(fake.is_closed());
}

// ---------------------------------------------------------------------------
// WebSocket URL contains ConnectionId from metadata
// Reference: f"{WSS_URL}&ConnectionId={connect_id()}..."
// ---------------------------------------------------------------------------

TEST(SynthesisSession, ConnectUrlContainsConnectionId) {
    FakeWebSocketClient fake;
    push_minimal_chunk(fake);
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());
    EXPECT_NE(fake.connected_url().find("ConnectionId="), std::string::npos);
}

TEST(SynthesisSession, ConnectUrlContainsSecMsGec) {
    FakeWebSocketClient fake;
    push_minimal_chunk(fake);
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());
    EXPECT_NE(fake.connected_url().find("Sec-MS-GEC="), std::string::npos);
    EXPECT_NE(fake.connected_url().find("Sec-MS-GEC-Version="), std::string::npos);
}

// ---------------------------------------------------------------------------
// Empty chunks list → success with empty result
// ---------------------------------------------------------------------------

TEST(SynthesisSession, EmptyChunksReturnsEmpty) {
    FakeWebSocketClient fake;
    auto session = make_session(fake);

    const std::vector<std::string> chunks;
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
    EXPECT_EQ(fake.connect_count(), 0);
}
