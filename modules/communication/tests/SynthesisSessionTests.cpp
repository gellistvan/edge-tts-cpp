#include "communication/SynthesisSession.hpp"
#include "communication/FakeWebSocketClient.hpp"
#include "communication/WebSocketClient.hpp"
#include "communication/EdgeProtocol.hpp"
#include "communication/EdgeServiceConfig.hpp"
#include "communication/EdgeTokenProvider.hpp"
#include "communication/ConnectionMetadata.hpp"
#include "common/Clock.hpp"
#include "common/Error.hpp"
#include "support/WebSocketFrameHelpers.hpp"
#include "common/IdGenerator.hpp"
#include "core/Chunk.hpp"
#include "core/TtsConfig.hpp"
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

using edge_tts::test::make_audio_frame;
using edge_tts::test::make_turn_end;
using edge_tts::test::make_word_boundary;
using edge_tts::test::to_bytes;

// Push a complete minimal chunk sequence: audio + turn.end
static void push_minimal_chunk(FakeWebSocketClient& fake) {
    fake.push_incoming(make_audio_frame(to_bytes("MP3DATA")));
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
    fake.push_incoming(make_audio_frame(to_bytes("AUDIODATA")));
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
    const auto body = to_bytes("AUDIOBYTES");
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
    fake.push_incoming(make_audio_frame(to_bytes("MP3")));
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
    fake.push_incoming(make_audio_frame(to_bytes("MP3")));
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
    fake.push_incoming(make_audio_frame(to_bytes("A")));
    fake.push_incoming(make_turn_end());
    // Extra messages that should NOT be consumed
    fake.push_incoming(make_audio_frame(to_bytes("B")));
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
// The service reports each chunk's boundary offsets relative to that chunk's
// start.  SynthesisSession converts them to absolute offsets by adding the
// cumulative subtitle end-tick from all previous chunks.
//
// Compensation for chunk N = sum of max(offset_ticks + duration_ticks) for
// all boundary events in chunks 0..N-1.  Audio byte counts play no role.
//
// Duration ticks are never modified.
// ---------------------------------------------------------------------------

// Helper: build a boundary frame with given raw offset and 500ms duration.
static WebSocketMessage make_boundary_at(std::int64_t raw_offset,
                                          const std::string& word = "word") {
    return make_word_boundary(raw_offset, 500'000, word);
}

// Helper: boundary frame with explicit duration.
static WebSocketMessage make_boundary_at_dur(std::int64_t raw_offset,
                                              std::int64_t duration,
                                              const std::string& word = "word") {
    return make_word_boundary(raw_offset, duration, word);
}

TEST(OffsetCompensation, FirstChunkBoundaryOffsetIsUnchanged) {
    // First chunk always has compensation = 0.
    FakeWebSocketClient fake;
    fake.push_incoming(make_boundary_at(1'234'567));
    fake.push_incoming(make_audio_frame("X"));
    fake.push_incoming(make_turn_end());
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"hello"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    ASSERT_TRUE(result.has_value());

    const auto& bc = std::get<BoundaryChunk>((*result)[0]);
    EXPECT_EQ(bc.offset_ticks, 1'234'567);
}

TEST(OffsetCompensation, SecondChunkOffsetShiftedByFirstChunkBoundaryEnd) {
    // Chunk 1: boundary at offset 2_000_000 ticks, duration 1_000_000 ticks
    //          → raw end = 3_000_000 ticks → compensation for chunk 2 = 3_000_000
    // Chunk 2: boundary at raw offset 500_000
    //          → global offset = 500_000 + 3_000_000 = 3_500_000
    constexpr std::int64_t chunk1_offset   = 2'000'000;
    constexpr std::int64_t chunk1_duration = 1'000'000;
    constexpr std::int64_t chunk1_end      = chunk1_offset + chunk1_duration;
    constexpr std::int64_t chunk2_raw      = 500'000;
    constexpr std::int64_t expected        = chunk2_raw + chunk1_end;

    FakeWebSocketClient fake;
    // Chunk 1: boundary + audio + turn.end
    fake.push_incoming(make_boundary_at_dur(chunk1_offset, chunk1_duration, "first"));
    fake.push_incoming(make_audio_frame("A1"));
    fake.push_incoming(make_turn_end());
    // Chunk 2: audio + boundary at raw offset chunk2_raw + turn.end
    fake.push_incoming(make_audio_frame("A2"));
    fake.push_incoming(make_boundary_at(chunk2_raw, "second"));
    fake.push_incoming(make_turn_end());

    auto session = make_session(fake);

    const std::vector<std::string> chunks{"chunk1", "chunk2"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    ASSERT_TRUE(result.has_value());

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
    EXPECT_EQ(second_boundary_offset, expected);
}

TEST(OffsetCompensation, MaxBoundaryEndDeterminesNextCompensation) {
    // When chunk 1 has multiple boundaries, the MAXIMUM end-tick drives
    // compensation — not the first or last received.
    // Chunk 1 boundaries:
    //   w1: offset=100, duration=200 → end=300
    //   w2: offset=500, duration=800 → end=1300  ← max
    //   w3: offset=200, duration=300 → end=500
    // Compensation for chunk 2 = 1300.
    FakeWebSocketClient fake;
    fake.push_incoming(make_boundary_at_dur(100, 200, "w1"));
    fake.push_incoming(make_boundary_at_dur(500, 800, "w2"));
    fake.push_incoming(make_boundary_at_dur(200, 300, "w3"));
    fake.push_incoming(make_audio_frame("A1"));
    fake.push_incoming(make_turn_end());
    // Chunk 2: boundary at raw offset 0
    fake.push_incoming(make_audio_frame("A2"));
    fake.push_incoming(make_boundary_at_dur(0, 100, "x"));
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

    // chunk1 boundaries pass through unmodified (compensation = 0)
    ASSERT_EQ(offsets.size(), 4u);
    EXPECT_EQ(offsets[0], 100);
    EXPECT_EQ(offsets[1], 500);
    EXPECT_EQ(offsets[2], 200);
    // chunk2 boundary: raw 0 + compensation 1300
    EXPECT_EQ(offsets[3], 1300);
}

TEST(OffsetCompensation, MultipleBoundariesInSecondChunkGetSameCompensation) {
    // All boundaries within chunk 2 receive the same compensation (derived
    // from chunk 1's boundary metadata).
    // Chunk 1: boundary at offset=4_000_000 ticks, duration=1_000_000 → end=5_000_000
    constexpr std::int64_t comp = 5'000'000;

    FakeWebSocketClient fake;
    fake.push_incoming(make_boundary_at_dur(4'000'000, 1'000'000, "c1w"));
    fake.push_incoming(make_audio_frame("A1"));
    fake.push_incoming(make_turn_end());
    // Chunk 2: three boundaries at raw 100, 200, 300 + audio + turn.end
    fake.push_incoming(make_boundary_at(100, "a"));
    fake.push_incoming(make_boundary_at(200, "b"));
    fake.push_incoming(make_boundary_at(300, "c"));
    fake.push_incoming(make_audio_frame("A2"));
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

    ASSERT_EQ(offsets.size(), 4u);
    EXPECT_EQ(offsets[0], 4'000'000);           // chunk 1, unmodified
    EXPECT_EQ(offsets[1], 100 + comp);
    EXPECT_EQ(offsets[2], 200 + comp);
    EXPECT_EQ(offsets[3], 300 + comp);
}

TEST(OffsetCompensation, DurationTicksUnchanged) {
    // duration_ticks must NEVER be modified by offset compensation.
    FakeWebSocketClient fake;
    // Chunk 1: boundary so compensation is non-zero for chunk 2.
    fake.push_incoming(make_boundary_at_dur(0, 500'000, "c1"));
    fake.push_incoming(make_audio_frame("A1"));
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
    fake.push_incoming(make_audio_frame("A2"));
    fake.push_incoming(make_turn_end());

    auto session = make_session(fake);

    const std::vector<std::string> chunks{"first", "second"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    ASSERT_TRUE(result.has_value());

    // The last boundary (from chunk 2) must have raw_duration unchanged.
    std::int64_t last_duration = -1;
    for (const auto& c : *result)
        if (std::holds_alternative<BoundaryChunk>(c))
            last_duration = std::get<BoundaryChunk>(c).duration_ticks;
    EXPECT_EQ(last_duration, raw_duration);
}

TEST(OffsetCompensation, NoBoundariesInChunkContributesZeroCompensation) {
    // If chunk 1 sends no boundary events, it contributes 0 to compensation.
    // Chunk 2's boundary at raw offset 500 should pass through as 500.
    FakeWebSocketClient fake;
    // Chunk 1: audio only, no boundaries.
    fake.push_incoming(make_audio_frame("AUDIO"));
    fake.push_incoming(make_turn_end());
    // Chunk 2: boundary at raw offset 500.
    fake.push_incoming(make_audio_frame("AUDIO2"));
    fake.push_incoming(make_boundary_at(500, "word"));
    fake.push_incoming(make_turn_end());

    auto session = make_session(fake);

    const std::vector<std::string> chunks{"c1", "c2"};
    const auto result = session.synthesize(TtsConfig::defaults(),
                                           std::span<const std::string>{chunks});
    ASSERT_TRUE(result.has_value());

    for (const auto& c : *result) {
        if (std::holds_alternative<BoundaryChunk>(c)) {
            EXPECT_EQ(std::get<BoundaryChunk>(c).offset_ticks, 500);
        }
    }
}

TEST(OffsetCompensation, ThreeChunksCumulativeCompensation) {
    // Compensation accumulates: chunk N's compensation = sum of all previous
    // chunks' last boundary end-ticks.
    // Chunk 1: boundary at 0, duration=1_000_000 → end=1_000_000 → comp2=1_000_000
    // Chunk 2: boundary at 0, duration=2_000_000 → end=2_000_000 → comp3=3_000_000
    constexpr std::int64_t comp2 = 1'000'000;
    constexpr std::int64_t comp3 = 3'000'000;

    FakeWebSocketClient fake;
    fake.push_incoming(make_boundary_at_dur(0, 1'000'000, "w1"));
    fake.push_incoming(make_audio_frame("A1"));
    fake.push_incoming(make_turn_end());
    fake.push_incoming(make_boundary_at_dur(0, 2'000'000, "w2"));
    fake.push_incoming(make_audio_frame("A2"));
    fake.push_incoming(make_turn_end());
    fake.push_incoming(make_audio_frame("A3"));
    fake.push_incoming(make_boundary_at_dur(0, 500'000, "w3"));
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
    EXPECT_EQ(offsets[1], comp2);   // chunk 2: compensation = 1_000_000
    EXPECT_EQ(offsets[2], comp3);   // chunk 3: compensation = 3_000_000
}

TEST(OffsetCompensation, AudioByteSizeDoesNotAffectSubtitleTimestamp) {
    // Regression: changing audio byte count must NOT change the subtitle offset.
    // Two sessions with identical boundary metadata but different audio sizes.
    // Both must produce the same boundary offset_ticks.
    auto run = [](std::size_t audio_size) -> std::int64_t {
        FakeWebSocketClient fake;
        // Chunk 1: boundary at 1_000_000 ticks (1 s), duration 500_000 (50 ms)
        fake.push_incoming(make_boundary_at_dur(1'000'000, 500'000, "c1w"));
        fake.push_incoming(make_audio_frame(std::string(audio_size, 'X')));
        fake.push_incoming(make_turn_end());
        // Chunk 2: boundary at raw offset 0
        fake.push_incoming(make_audio_frame("A2"));
        fake.push_incoming(make_boundary_at_dur(0, 200'000, "c2w"));
        fake.push_incoming(make_turn_end());

        auto session = make_session(fake);
        const std::vector<std::string> chunks{"c1", "c2"};
        auto result = session.synthesize(TtsConfig::defaults(),
                                         std::span<const std::string>{chunks});
        // Return the offset of the last boundary (chunk 2's boundary).
        std::int64_t last_offset = -1;
        for (const auto& c : *result)
            if (std::holds_alternative<BoundaryChunk>(c))
                last_offset = std::get<BoundaryChunk>(c).offset_ticks;
        return last_offset;
    };

    const std::int64_t offset_small = run(100);
    const std::int64_t offset_large = run(600'000);  // 600 KB — very different

    EXPECT_EQ(offset_small, offset_large);
    // Expected: 0 + (1_000_000 + 500_000) = 1_500_000
    EXPECT_EQ(offset_small, 1'500'000);
}

// ---------------------------------------------------------------------------
// 403 DRM retry behavior
//
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

// ---------------------------------------------------------------------------
// Proxy rejection — real WebSocketClient (transport-level guard, defense-in-depth)
//
// The primary proxy rejection is at the API layer (SpeechSynthesizer::run_pipeline).
// These tests verify the transport-level guard: a proxy set in
// WebSocketClientOptions is still rejected at connect() time with
// ErrorCode::unsupported, ensuring defense-in-depth for callers that use
// WebSocketClient or SynthesisSession directly.
//
// Guarded by EDGE_TTS_HAVE_IXWEBSOCKET because the proxy guard lives in the
// ixwebsocket-backed connect() implementation.
// ---------------------------------------------------------------------------

#ifdef EDGE_TTS_HAVE_IXWEBSOCKET

using edge_tts::communication::WebSocketClient;
using edge_tts::communication::WebSocketClientOptions;

// Helper: build a SynthesisSession backed by a real WebSocketClient.
static SynthesisSession make_session_real_ws(WebSocketClient& ws) {
    return SynthesisSession{
        ws, get_protocol(), make_test_config(),
        get_token_provider(), get_meta_factory(), g_clock};
}

// ---------------------------------------------------------------------------
// Proxy rejection — real WebSocketClient wired into SynthesisSession.
//
// These tests verify that a proxy URL set in WebSocketClientOptions is
// rejected at connect() time with ErrorCode::unsupported, and that
// SynthesisSession propagates the error rather than swallowing it.
// ---------------------------------------------------------------------------

TEST(SynthesisSession, ProxyRejectedByRealWebSocketClient) {
    // connect() fires the proxy guard before any network call — the error code
    // must be unsupported (not network_error or anything else).
    WebSocketClientOptions opts;
    opts.proxy = "http://proxy.example.com:3128";
    WebSocketClient real_ws{std::move(opts)};
    auto session = make_session_real_ws(real_ws);

    const std::vector<std::string> chunks{"hello"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::unsupported);
}

TEST(SynthesisSession, ProxyErrorMessageMentionsProxy) {
    WebSocketClientOptions opts;
    opts.proxy = "http://proxy.example.com:3128";
    WebSocketClient real_ws{std::move(opts)};
    auto session = make_session_real_ws(real_ws);

    const std::vector<std::string> chunks{"hello"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});

    EXPECT_FALSE(result.has_value());
    const std::string msg = result.error().what();
    const bool mentions_proxy = msg.find("proxy") != std::string::npos
                             || msg.find("Proxy") != std::string::npos;
    EXPECT_TRUE(mentions_proxy);
}

TEST(SynthesisSession, ProxyCredentialsRedactedInErrorContext) {
    // Credentials (user:pass@) in the proxy URL must be replaced by
    // [credentials] before appearing in any error field.
    WebSocketClientOptions opts;
    opts.proxy = "http://user:s3cr3t@proxy.internal:3128";
    WebSocketClient real_ws{std::move(opts)};
    auto session = make_session_real_ws(real_ws);

    const std::vector<std::string> chunks{"hello"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});

    EXPECT_FALSE(result.has_value());
    const std::string ctx = std::string(result.error().context());
    EXPECT_EQ(ctx.find("s3cr3t"), std::string::npos);
    EXPECT_NE(ctx.find("[credentials]"), std::string::npos);
}

TEST(SynthesisSession, NoProxyDoesNotTriggerUnsupported) {
    // Without a proxy, connect() reaches the network and will fail on a fake
    // host — but the error must NOT be unsupported (proxy guard must not fire).
    WebSocketClient real_ws;  // default WebSocketClientOptions — proxy absent

    // Redirect to a guaranteed-unreachable host so the test is fast.
    EdgeServiceConfig cfg = make_test_config();
    cfg.websocket_endpoint = "wss://this-host-does-not-exist-edge-tts.invalid?x=1";

    SynthesisSession session{
        real_ws, get_protocol(), cfg,
        get_token_provider(), get_meta_factory(), g_clock};

    const std::vector<std::string> chunks{"hello"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});

    EXPECT_FALSE(result.has_value());
    EXPECT_NE(result.error().code(), ErrorCode::unsupported);
}

#endif  // EDGE_TTS_HAVE_IXWEBSOCKET
