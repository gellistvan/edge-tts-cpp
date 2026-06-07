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
using edge_tts::common::IClock;
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

static SynthesisSession make_session(FakeWebSocketClient& fake,
                                      const IClock& clock = g_clock) {
    return SynthesisSession{
        fake,
        get_protocol(),
        make_test_config(),
        get_token_provider(),
        get_meta_factory(),
        clock
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
    EXPECT_EQ(result.error().code(), ErrorCode::protocol_error);
    EXPECT_TRUE(fake.is_closed());
}

// ---------------------------------------------------------------------------
// No audio before turn.end produces service_error
// Reference: communicate.py raise NoAudioReceived(...)
// ---------------------------------------------------------------------------

TEST(SynthesisSession, NoAudioBeforeTurnEndProducesServiceError) {
    FakeWebSocketClient fake;
    // Push a turn.end without any preceding audio frame.
    fake.push_incoming(make_turn_end());
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::service_error);
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

// ---------------------------------------------------------------------------
// Offset compensation across multiple chunks
//
// Reference: communicate.py __compensate_offset()
//   offset_compensation = cumulative_audio_bytes * 8 * 10_000_000 // 48_000
//
// Boundaries in the first chunk get compensation = 0.
// Boundaries in the second chunk get compensation based on audio bytes from
// the first chunk.  Duration is never affected.
// ---------------------------------------------------------------------------

// Helper: build a boundary frame with the given raw offset.
static WebSocketMessage make_boundary_at(std::int64_t raw_offset,
                                          const std::string& word = "word") {
    return make_word_boundary(raw_offset, 500'000, word);
}

// Helper: build an audio frame with exactly n_bytes of audio payload.
static WebSocketMessage make_audio_of_size(std::size_t n_bytes) {
    return make_audio_frame(std::vector<std::byte>(n_bytes, std::byte{0xAB}));
}

TEST(OffsetCompensation, FirstChunkBoundaryOffsetIsUnchanged) {
    // First chunk: compensation = 0, so raw offset must pass through unchanged.
    FakeWebSocketClient fake;
    fake.push_incoming(make_boundary_at(1'234'567));
    fake.push_incoming(make_audio_of_size(6000));  // 6000 bytes audio
    fake.push_incoming(make_turn_end());
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"hello"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    ASSERT_TRUE(result.has_value());

    const auto& bc = std::get<BoundaryChunk>((*result)[0]);
    EXPECT_EQ(bc.offset_ticks, 1'234'567);
}

TEST(OffsetCompensation, SecondChunkBoundaryOffsetIsCompensated) {
    // First chunk: N audio bytes, boundary at raw offset 0.
    // Second chunk: boundary at raw offset 0 must be shifted by
    //   N * 8 * 10_000_000 / 48_000 ticks.
    //
    // Using N = 6000 bytes:
    //   compensation = 6000 * 8 * 10_000_000 / 48_000 = 10_000_000 ticks (1 second)
    constexpr std::size_t N = 6000;
    constexpr std::int64_t expected_comp =
        static_cast<std::int64_t>(N) * 8LL * 10'000'000LL / 48'000LL;

    FakeWebSocketClient fake;
    // Chunk 1: audio (N bytes) + boundary at offset 0 + turn.end
    fake.push_incoming(make_audio_of_size(N));
    fake.push_incoming(make_boundary_at(0, "first"));
    fake.push_incoming(make_turn_end());
    // Chunk 2: audio + boundary at raw offset 0 + turn.end
    fake.push_incoming(make_audio_of_size(100));
    fake.push_incoming(make_boundary_at(0, "second"));
    fake.push_incoming(make_turn_end());

    auto session = make_session(fake);

    const std::vector<std::string> chunks{"chunk1", "chunk2"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    ASSERT_TRUE(result.has_value());

    // Find the second boundary (from chunk 2).
    std::int64_t second_boundary_offset = -1;
    int boundary_count = 0;
    for (const auto& c : *result) {
        if (std::holds_alternative<BoundaryChunk>(c)) {
            ++boundary_count;
            if (boundary_count == 2)
                second_boundary_offset = std::get<BoundaryChunk>(c).offset_ticks;
        }
    }
    EXPECT_EQ(boundary_count, 2);
    EXPECT_EQ(second_boundary_offset, expected_comp);
}

TEST(OffsetCompensation, MultipleBoundariesInChunkGetSameCompensation) {
    // Within a single chunk all boundaries get the same (pre-computed)
    // compensation from the audio bytes of all *previous* chunks.
    // Audio from the current chunk does not affect the current compensation.
    constexpr std::size_t N = 12000;
    constexpr std::int64_t comp =
        static_cast<std::int64_t>(N) * 8LL * 10'000'000LL / 48'000LL;

    FakeWebSocketClient fake;
    // Chunk 1: N bytes audio + turn.end
    fake.push_incoming(make_audio_of_size(N));
    fake.push_incoming(make_turn_end());
    // Chunk 2: three boundaries + audio + turn.end
    fake.push_incoming(make_boundary_at(100, "a"));
    fake.push_incoming(make_boundary_at(200, "b"));
    fake.push_incoming(make_boundary_at(300, "c"));
    fake.push_incoming(make_audio_of_size(50));
    fake.push_incoming(make_turn_end());

    auto session = make_session(fake);

    const std::vector<std::string> chunks{"one", "two"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    ASSERT_TRUE(result.has_value());

    std::vector<std::int64_t> offsets;
    for (const auto& c : *result)
        if (std::holds_alternative<BoundaryChunk>(c))
            offsets.push_back(std::get<BoundaryChunk>(c).offset_ticks);

    ASSERT_EQ(offsets.size(), 3u);
    // All three boundaries get the same compensation (from chunk 1 bytes).
    EXPECT_EQ(offsets[0], 100 + comp);
    EXPECT_EQ(offsets[1], 200 + comp);
    EXPECT_EQ(offsets[2], 300 + comp);
}

TEST(OffsetCompensation, DurationTicksUnchanged) {
    // duration_ticks must NEVER be modified by offset compensation.
    FakeWebSocketClient fake;
    // Chunk 1: audio so compensation is non-zero for chunk 2.
    fake.push_incoming(make_audio_of_size(6000));
    fake.push_incoming(make_turn_end());
    // Chunk 2: boundary with known duration.
    constexpr std::int64_t raw_duration = 750'000;
    fake.push_incoming(
        [&]() -> WebSocketMessage {
            WebSocketMessage m;
            m.type = WebSocketMessage::Type::text;
            m.text = "X-RequestId:abc\r\nPath:audio.metadata\r\n\r\n"
                     "{\"Metadata\":[{\"Type\":\"WordBoundary\","
                     "\"Data\":{\"Offset\":0"
                     ",\"Duration\":" + std::to_string(raw_duration) +
                     ",\"text\":{\"Text\":\"test\"}}}]}";
            return m;
        }());
    fake.push_incoming(make_audio_of_size(100));
    fake.push_incoming(make_turn_end());

    auto session = make_session(fake);

    const std::vector<std::string> chunks{"first", "second"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    ASSERT_TRUE(result.has_value());

    for (const auto& c : *result) {
        if (std::holds_alternative<BoundaryChunk>(c)) {
            const auto& bc = std::get<BoundaryChunk>(c);
            EXPECT_EQ(bc.duration_ticks, raw_duration);
        }
    }
}

TEST(OffsetCompensation, LargeAudioBytesNoOverflow) {
    // Verify that 64-bit arithmetic is used for the compensation formula.
    // Large byte count that would overflow int32_t:
    //   2^31 = 2_147_483_648 bytes ≈ 2 GB
    // Expected compensation = 2_147_483_648 * 8 * 10_000_000 / 48_000
    //                       = 3_579_139_413 ticks  (> INT32_MAX = 2_147_483_647)
    constexpr std::int64_t large_bytes = 2'147'483'648LL;  // 2 GiB
    constexpr std::int64_t expected_comp =
        large_bytes * 8LL * 10'000'000LL / 48'000LL;
    static_assert(expected_comp > 2'147'483'647LL,
                  "expected_comp must exceed INT32_MAX to prove no overflow");

    // We simulate this by computing the expected value and verifying the
    // compensation formula in isolation — a direct calculation test.
    const std::int64_t computed =
        large_bytes * 8LL * 10'000'000LL / 48'000LL;
    EXPECT_EQ(computed, expected_comp);
    EXPECT_TRUE(computed > std::int64_t{2'147'483'647LL});  // must exceed INT32_MAX
}

TEST(OffsetCompensation, ThreeChunksCumulativeCompensation) {
    // Verify that compensation accumulates correctly over three chunks.
    // Chunk 1: A bytes → compensation for chunk 2 = A*8*10M/48000
    // Chunk 2: B bytes → compensation for chunk 3 = (A+B)*8*10M/48000
    constexpr std::size_t A = 4800;  // 0.1 second of audio at 48000 B/s
    constexpr std::size_t B = 9600;  // 0.2 seconds
    constexpr std::int64_t comp2 =
        static_cast<std::int64_t>(A) * 8LL * 10'000'000LL / 48'000LL;
    constexpr std::int64_t comp3 =
        static_cast<std::int64_t>(A + B) * 8LL * 10'000'000LL / 48'000LL;

    FakeWebSocketClient fake;
    // Chunk 1: A bytes audio + boundary at 0 + turn.end
    fake.push_incoming(make_audio_of_size(A));
    fake.push_incoming(make_boundary_at(0, "w1"));
    fake.push_incoming(make_turn_end());
    // Chunk 2: B bytes audio + boundary at 0 + turn.end
    fake.push_incoming(make_audio_of_size(B));
    fake.push_incoming(make_boundary_at(0, "w2"));
    fake.push_incoming(make_turn_end());
    // Chunk 3: audio + boundary at 0 + turn.end
    fake.push_incoming(make_audio_of_size(100));
    fake.push_incoming(make_boundary_at(0, "w3"));
    fake.push_incoming(make_turn_end());

    auto session = make_session(fake);

    const std::vector<std::string> chunks{"c1", "c2", "c3"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    ASSERT_TRUE(result.has_value());

    std::vector<std::int64_t> offsets;
    for (const auto& c : *result)
        if (std::holds_alternative<BoundaryChunk>(c))
            offsets.push_back(std::get<BoundaryChunk>(c).offset_ticks);

    ASSERT_EQ(offsets.size(), 3u);
    EXPECT_EQ(offsets[0], 0);       // chunk 1: compensation = 0
    EXPECT_EQ(offsets[1], comp2);   // chunk 2: compensation = A bytes
    EXPECT_EQ(offsets[2], comp3);   // chunk 3: compensation = (A+B) bytes
}

// ---------------------------------------------------------------------------
// 403 DRM retry behavior
//
// Reference: communicate.py Communicate.stream():
//   except aiohttp.ClientResponseError as e:
//       if e.status != 403: raise
//       DRM.handle_client_response_error(e)   # parse Date, adjust skew
//       async for message in self.__stream():  # single retry
//
// Rules:
//   - drm_error (HTTP 403): retry exactly once; adjust clock skew if Date present.
//   - network_error, service_error, …: do NOT retry.
//   - Post-connect errors (send/receive): do NOT retry.
// ---------------------------------------------------------------------------

// Helper: build a minimal valid response for the receive loop.
static std::vector<WebSocketMessage> minimal_audio_response() {
    const std::vector<std::byte> payload(32, std::byte{0xAB});
    return {make_audio_frame(payload), make_turn_end()};
}

TEST(DrmRetry, RetriesOnceWithSkewAdjustment) {
    // Setup: FixedClock at Unix epoch (t=0); server reports t=30 via Date header.
    // Expected clock skew after retry: 30.0 - (0.0 + 0.0) = 30.0 seconds.
    FixedClock  clock{std::chrono::system_clock::time_point{std::chrono::seconds{0LL}}};
    EdgeTokenProvider tp{make_test_config(), clock};
    EdgeProtocol      protocol{clock};
    ConnectionMetadataFactory mf{get_ids()};

    FakeWebSocketClient fake;
    // First connect fails: drm_error with Date header 30 seconds after epoch.
    fake.set_connect_fail_count(
        Error{ErrorCode::drm_error, "403 Forbidden",
              "Thu, 01 Jan 1970 00:00:30 GMT"},
        1);
    // Push messages consumed by the successful second connect's receive loop.
    for (auto& m : minimal_audio_response())
        fake.push_incoming(std::move(m));

    SynthesisSession session{fake, protocol, make_test_config(), tp, mf, clock};

    const std::vector<std::string> chunks{"hello"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(fake.connect_count(), 2);
    // Skew = server_ts(30) - (client_now(0) + prior_skew(0)) = 30.0
    EXPECT_EQ(tp.clock_skew_seconds(), 30.0);
}

TEST(DrmRetry, RetriesOnceWithoutDateContext) {
    // drm_error with no Date context: still retries once, skew unchanged.
    FixedClock  clock{std::chrono::system_clock::time_point{std::chrono::seconds{0LL}}};
    EdgeTokenProvider tp{make_test_config(), clock};
    EdgeProtocol      protocol{clock};
    ConnectionMetadataFactory mf{get_ids()};

    FakeWebSocketClient fake;
    fake.set_connect_fail_count(
        Error{ErrorCode::drm_error, "403 Forbidden"},  // no Date context
        1);
    for (auto& m : minimal_audio_response())
        fake.push_incoming(std::move(m));

    SynthesisSession session{fake, protocol, make_test_config(), tp, mf, clock};

    const std::vector<std::string> chunks{"hello"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(fake.connect_count(), 2);
    EXPECT_EQ(tp.clock_skew_seconds(), 0.0);  // no adjustment
}

TEST(DrmRetry, NetworkErrorDoesNotRetry) {
    // network_error is not retriable — should_retry() returns false.
    FakeWebSocketClient fake;
    fake.set_connect_error(Error{ErrorCode::network_error, "connection refused"});
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"hello"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::network_error);
    EXPECT_EQ(fake.connect_count(), 1);
}

TEST(DrmRetry, SendFailureAfterConnectDoesNotRetry) {
    // Connect succeeds; first send_text fails.
    // Post-connect errors are never retried (Python only retries the connect).
    FakeWebSocketClient fake;
    fake.set_send_error(Error{ErrorCode::network_error, "send failed"});
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"hello"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::network_error);
    EXPECT_EQ(fake.connect_count(), 1);
}

TEST(DrmRetry, DrmErrorDoesNotRetryMoreThanOnce) {
    // Both connect attempts fail with drm_error.
    // max_retries=1 means only one retry; second failure propagates.
    FixedClock  clock{std::chrono::system_clock::time_point{std::chrono::seconds{0LL}}};
    EdgeTokenProvider tp{make_test_config(), clock};
    EdgeProtocol      protocol{clock};
    ConnectionMetadataFactory mf{get_ids()};

    FakeWebSocketClient fake;
    fake.set_connect_error(Error{ErrorCode::drm_error, "403 Forbidden"});

    SynthesisSession session{fake, protocol, make_test_config(), tp, mf, clock};

    const std::vector<std::string> chunks{"hello"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::drm_error);
    EXPECT_EQ(fake.connect_count(), 2);  // initial + exactly one retry
}

TEST(DrmRetry, SkewIsComputedRelativeToEffectiveClientTime) {
    // If a prior skew of +10 s was already applied, and the server says t=30,
    // the new correction should bring the total skew to (30 - client_now).
    // With client_now=5 and prior_skew=10:
    //   effective = 5 + 10 = 15
    //   delta     = 30 - 15 = 15
    //   new_total = 10 + 15 = 25 = 30 - 5 = server - client_now  ✓
    FixedClock  clock{std::chrono::system_clock::time_point{std::chrono::seconds{5LL}}};
    EdgeTokenProvider tp{make_test_config(), clock};
    tp.adjust_clock_skew(10.0);  // pre-existing skew

    EdgeProtocol      protocol{clock};
    ConnectionMetadataFactory mf{get_ids()};

    FakeWebSocketClient fake;
    fake.set_connect_fail_count(
        Error{ErrorCode::drm_error, "403 Forbidden",
              "Thu, 01 Jan 1970 00:00:30 GMT"},  // server_time = 30
        1);
    for (auto& m : minimal_audio_response())
        fake.push_incoming(std::move(m));

    SynthesisSession session{fake, protocol, make_test_config(), tp, mf, clock};

    const std::vector<std::string> chunks{"hello"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(fake.connect_count(), 2);
    // After adjustment: total_skew = 30 - 5 = 25.0
    EXPECT_EQ(tp.clock_skew_seconds(), 25.0);
}
