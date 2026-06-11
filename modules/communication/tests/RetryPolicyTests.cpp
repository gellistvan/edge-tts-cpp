// Tests for RetryPolicy and its integration with SynthesisSession.
//
//   - Only ClientResponseError(status=403) triggers a retry.
//   - Exactly one retry per chunk.
//   - All other errors propagate immediately.
//   - No sleep between attempts.

#include "communication/RetryPolicy.hpp"
#include "communication/SynthesisSession.hpp"
#include "communication/FakeWebSocketClient.hpp"
#include "communication/EdgeProtocol.hpp"
#include "communication/EdgeServiceConfig.hpp"
#include "communication/EdgeTokenProvider.hpp"
#include "communication/ConnectionMetadata.hpp"
#include "common/Clock.hpp"
#include "common/Error.hpp"
#include "common/IdGenerator.hpp"
#include "core/Chunk.hpp"
#include "core/TtsConfig.hpp"
#include "support/WebSocketFrameHelpers.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <chrono>
#include <span>
#include <string>
#include <variant>
#include <vector>

using edge_tts::communication::ConnectionMetadataFactory;
using edge_tts::communication::EdgeProtocol;
using edge_tts::communication::EdgeTokenProvider;
using edge_tts::communication::FakeWebSocketClient;
using edge_tts::communication::RetryPolicy;
using edge_tts::communication::SynthesisSession;
using edge_tts::communication::WebSocketMessage;
using edge_tts::common::Error;
using edge_tts::common::ErrorCode;
using edge_tts::common::FixedClock;
using edge_tts::common::IdGenerator;
using edge_tts::core::AudioChunk;
using edge_tts::core::TtsConfig;
using edge_tts::core::TtsChunk;
using edge_tts::test::make_audio_frame;
using edge_tts::test::make_turn_end;

// ---------------------------------------------------------------------------
// Shared fixtures
// ---------------------------------------------------------------------------

static FixedClock g_retry_clock{
    std::chrono::system_clock::time_point{std::chrono::seconds{1705400000LL}}};

static EdgeProtocol& get_protocol() {
    static EdgeProtocol proto{g_retry_clock};
    return proto;
}

static EdgeTokenProvider& get_token_provider() {
    static EdgeTokenProvider tp{
        edge_tts::communication::default_edge_service_config(),
        g_retry_clock};
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
                                     RetryPolicy policy = {}) {
    return SynthesisSession{
        fake,
        get_protocol(),
        edge_tts::communication::default_edge_service_config(),
        get_token_provider(),
        get_meta_factory(),
        g_retry_clock,
        {},
        policy
    };
}

static void push_success(FakeWebSocketClient& fake) {
    fake.push_incoming(make_audio_frame("AUDIO"));
    fake.push_incoming(make_turn_end());
}

// ---------------------------------------------------------------------------
// RetryPolicy unit tests (no network, no SynthesisSession)
// ---------------------------------------------------------------------------

TEST(RetryPolicy, DefaultMaxRetriesIsOne) {
    // Exactly one retry allowed by default.
    RetryPolicy p;
    EXPECT_EQ(p.max_retries, 1);
}

TEST(RetryPolicy, ShouldRetryDrmErrorOnFirstAttempt) {
    // drm_error (HTTP 403) IS retried.
    RetryPolicy p;
    Error e{ErrorCode::drm_error, "token rejected"};
    EXPECT_TRUE(p.should_retry(e, 0));
}

TEST(RetryPolicy, ShouldNotRetryNetworkError) {
    // Non-DRM errors propagate immediately.
    RetryPolicy p;
    Error e{ErrorCode::network_error, "connection refused"};
    EXPECT_FALSE(p.should_retry(e, 0));
}

TEST(RetryPolicy, ShouldNotRetryProtocolError) {
    RetryPolicy p;
    Error e{ErrorCode::protocol_error, "bad frame"};
    EXPECT_FALSE(p.should_retry(e, 0));
}

TEST(RetryPolicy, ShouldNotRetryServiceError) {
    // service_error (NoAudioReceived) is NOT a 403 — no retry.
    RetryPolicy p;
    Error e{ErrorCode::service_error, "no audio"};
    EXPECT_FALSE(p.should_retry(e, 0));
}

TEST(RetryPolicy, ShouldNotRetryDrmErrorWhenMaxRetriesExhausted) {
    // attempt == max_retries → no more retries.
    RetryPolicy p;
    p.max_retries = 1;
    Error e{ErrorCode::drm_error, "token rejected"};
    EXPECT_FALSE(p.should_retry(e, 1));  // attempt 1 = second failure = no more
}

TEST(RetryPolicy, CustomMaxRetriesIsRespected) {
    RetryPolicy p;
    p.max_retries = 0;
    Error e{ErrorCode::drm_error, "token rejected"};
    EXPECT_FALSE(p.should_retry(e, 0));  // 0 retries allowed → never retry
}

// ---------------------------------------------------------------------------
// EdgeTokenProvider.adjust_clock_skew changes the token
// ---------------------------------------------------------------------------

TEST(EdgeTokenProvider, AdjustClockSkewChangesToken) {
    FixedClock clock{std::chrono::system_clock::time_point{std::chrono::seconds{1700000000LL}}};
    EdgeTokenProvider tp{edge_tts::communication::default_edge_service_config(), clock};

    auto before = tp.sec_ms_gec();
    EXPECT_TRUE(before.has_value());

    // Advance clock by 300 s (one 5-minute bucket boundary).
    tp.adjust_clock_skew(300.0);

    auto after = tp.sec_ms_gec();
    EXPECT_TRUE(after.has_value());

    // Tokens must differ because the bucket changed.
    EXPECT_NE(*before, *after);
}

TEST(EdgeTokenProvider, AdjustClockSkewZeroIsNoop) {
    FixedClock clock{std::chrono::system_clock::time_point{std::chrono::seconds{1700000000LL}}};
    EdgeTokenProvider tp{edge_tts::communication::default_edge_service_config(), clock};

    auto before = tp.sec_ms_gec();
    tp.adjust_clock_skew(0.0);
    auto after = tp.sec_ms_gec();
    EXPECT_EQ(*before, *after);
}

TEST(EdgeTokenProvider, AccumulatedSkewIsAccessible) {
    FixedClock clock{std::chrono::system_clock::time_point{std::chrono::seconds{0LL}}};
    EdgeTokenProvider tp{edge_tts::communication::default_edge_service_config(), clock};

    EXPECT_EQ(tp.clock_skew_seconds(), 0.0);
    tp.adjust_clock_skew(10.0);
    EXPECT_EQ(tp.clock_skew_seconds(), 10.0);
    tp.adjust_clock_skew(5.0);
    EXPECT_EQ(tp.clock_skew_seconds(), 15.0);
}

// ---------------------------------------------------------------------------
// SynthesisSession + RetryPolicy integration tests
// ---------------------------------------------------------------------------

TEST(SynthesisSessionRetry, SuccessWithoutRetry) {
    // Happy path: no error → result OK, connect called once.
    FakeWebSocketClient fake;
    push_success(fake);
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(fake.connect_count(), 1);
}

TEST(SynthesisSessionRetry, DrmErrorTriggersOneRetry) {
    // First connect fails with drm_error (HTTP 403), second succeeds.
    FakeWebSocketClient fake;
    fake.set_connect_fail_count(Error{ErrorCode::drm_error, "403"}, 1);
    push_success(fake);  // queued for the second (retried) connect
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(fake.connect_count(), 2);
}

TEST(SynthesisSessionRetry, TokenRegeneratedOnRetry) {
    // Each connect attempt uses a fresh sec_ms_gec() call.
    // The ConnectionId in the URL must be different between attempts
    // (new UUID from metadata_factory each iteration).
    FakeWebSocketClient fake;
    fake.set_connect_fail_count(Error{ErrorCode::drm_error, "403"}, 1);
    push_success(fake);
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(fake.connect_count(), 2);

    // Both URLs must contain ConnectionId= (token is regenerated per attempt).
    const auto& urls = fake.connect_urls();
    EXPECT_EQ(urls.size(), 2u);
    EXPECT_NE(urls[0].find("ConnectionId="), std::string::npos);
    EXPECT_NE(urls[1].find("ConnectionId="), std::string::npos);
    // ConnectionIds are unique UUIDs — the two URLs must differ.
    EXPECT_NE(urls[0], urls[1]);
}

TEST(SynthesisSessionRetry, MaxRetriesEnforced) {
    // Two consecutive drm_errors with max_retries=1 → fails after 2 attempts.
    FakeWebSocketClient fake;
    fake.set_connect_error(Error{ErrorCode::drm_error, "403 persistent"});
    auto session = make_session(fake);  // default max_retries=1

    const std::vector<std::string> chunks{"Hello"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::drm_error);
    // 1 original attempt + 1 retry = 2 total (max_retries=1).
    EXPECT_EQ(fake.connect_count(), 2);
}

TEST(SynthesisSessionRetry, NonRetryableErrorFailsImmediately) {
    // network_error is not retried.
    FakeWebSocketClient fake;
    fake.set_connect_error(Error{ErrorCode::network_error, "refused"});
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code(), ErrorCode::network_error);
    // No retry — exactly one attempt.
    EXPECT_EQ(fake.connect_count(), 1);
}

TEST(SynthesisSessionRetry, RetryPreservesChunksFromEarlierSuccesses) {
    // chunk 1 succeeds immediately; chunk 2 first fails with drm_error then
    // succeeds on retry.  The result must contain audio from both chunks.
    FakeWebSocketClient fake;

    // chunk 1: success on first connect
    push_success(fake);

    // chunk 2: first connect → drm_error; second connect → success
    fake.set_connect_fail_count(Error{ErrorCode::drm_error, "403"}, 1);
    push_success(fake);

    auto session = make_session(fake);

    const std::vector<std::string> chunks{"first", "second"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});
    EXPECT_TRUE(result.has_value());
    // 1 connect for chunk1 + 2 connects for chunk2 (1 retry) = 3 total.
    EXPECT_EQ(fake.connect_count(), 3);
    // Both chunks produced audio.
    int audio_count = 0;
    for (const auto& c : *result)
        if (std::holds_alternative<AudioChunk>(c)) ++audio_count;
    EXPECT_EQ(audio_count, 2);
}

TEST(SynthesisSessionRetry, NoRetryWhenMaxRetriesIsZero) {
    // RetryPolicy{max_retries=0}: even drm_error fails immediately.
    FakeWebSocketClient fake;
    fake.set_connect_error(Error{ErrorCode::drm_error, "403"});
    RetryPolicy policy;
    policy.max_retries = 0;
    auto session = make_session(fake, policy);

    const std::vector<std::string> chunks{"Hello"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(fake.connect_count(), 1);  // no retry, one attempt only
}

TEST(SynthesisSessionRetry, PostConnectErrorIsNotRetried) {
    // Errors after a successful connect (send/receive failures) are NOT retried.
    FakeWebSocketClient fake;
    // connect succeeds, but receive fails with network_error
    fake.set_receive_error(Error{ErrorCode::network_error, "dropped"});
    auto session = make_session(fake);

    const std::vector<std::string> chunks{"Hello"};
    auto result = session.synthesize(TtsConfig::defaults(),
                                     std::span<const std::string>{chunks});
    EXPECT_FALSE(result.has_value());
    // Only one connect attempt — the error came after connect, not retried.
    EXPECT_EQ(fake.connect_count(), 1);
}
