// Tests for timeout propagation and cancellation behavior.
//
// Acceptance criteria verified here:
//   - A timeout from WebSocket receive() propagates through SynthesisSession
//     and SpeechSynthesizer as ErrorCode::timeout.
//   - Calling SpeechSynthesizer::cancel() before synthesize()/save() returns
//     ErrorCode::cancelled immediately (pre-synthesis check point).
//   - Calling cancel() before a SynthesisSession starts a chunk returns
//     ErrorCode::cancelled (between-chunk check point).
//   - A cancelled error from WebSocket receive() propagates as
//     ErrorCode::cancelled (within-chunk receive check point).
//   - cancel() is idempotent — calling it multiple times is harmless.
//   - Cancellation does not affect subsequent SpeechSynthesizer objects.
//
// All tests use FakeWebSocketClient or injected SynthesizerFn; no real network
// delays are required.

#include "api/SpeechSynthesizer.hpp"
#include "api/SynthesisOptions.hpp"
#include "common/CancellationToken.hpp"
#include "common/Error.hpp"
#include "common/Clock.hpp"
#include "common/IdGenerator.hpp"
#include "communication/ConnectionMetadata.hpp"
#include "communication/EdgeProtocol.hpp"
#include "communication/EdgeServiceConfig.hpp"
#include "communication/EdgeTokenProvider.hpp"
#include "communication/FakeWebSocketClient.hpp"
#include "communication/SynthesisSession.hpp"
#include "support/WebSocketFrameHelpers.hpp"
#include "core/Chunk.hpp"
#include "core/TtsConfig.hpp"
#include "vendor/minigtest/minigtest.hpp"
#include "ApiTestFixtures.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

using edge_tts::api::SpeechSynthesizer;
using edge_tts::api::SynthesisOptions;
using edge_tts::api::SynthesizerFn;
using edge_tts::common::CancellationToken;
using edge_tts::common::Error;
using edge_tts::common::ErrorCode;
using edge_tts::common::SystemClock;
using edge_tts::common::IdGenerator;
using edge_tts::communication::ConnectionMetadataFactory;
using edge_tts::communication::EdgeProtocol;
using edge_tts::communication::EdgeServiceConfig;
using edge_tts::communication::EdgeTokenProvider;
using edge_tts::communication::FakeWebSocketClient;
using edge_tts::communication::SynthesisSession;
using edge_tts::core::AudioChunk;
using edge_tts::core::TtsChunk;
using edge_tts::core::TtsConfig;

using edge_tts::test::make_audio_frame;
using edge_tts::test::make_turn_end;
using edge_tts::test::to_bytes;
using edge_tts::test::push_session;
using edge_tts::test::valid_config;

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Shared fixtures
// ---------------------------------------------------------------------------

// Build a SynthesisSession backed by a FakeWebSocketClient.
static SynthesisSession make_session(FakeWebSocketClient& fake,
                                     CancellationToken token = {})
{
    static SystemClock              clock;
    static IdGenerator              ids;
    static EdgeServiceConfig        svc = edge_tts::communication::default_edge_service_config();
    static EdgeTokenProvider        tp{svc, clock};
    static EdgeProtocol             proto{clock};
    static ConnectionMetadataFactory factory{ids};
    return SynthesisSession{fake, proto, svc, tp, factory, clock, std::move(token)};
}

// ---------------------------------------------------------------------------
// Timeout propagation
// ---------------------------------------------------------------------------

TEST(Timeout, ReceiveTimeoutPropagatesThroughSession) {
    // Inject a timeout error into the receive queue.  The session must
    // propagate it as ErrorCode::timeout without wrapping or replacing it.
    FakeWebSocketClient fake;
    fake.set_receive_error(Error{ErrorCode::timeout, "WebSocket receive timed out"});

    auto session = make_session(fake);
    const std::vector<std::string> chunks{"hello"};
    auto r = session.synthesize(valid_config(), chunks);

    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::timeout);
}

TEST(Timeout, ReceiveTimeoutPropagatesThroughSpeechSynthesizer) {
    // Verify that timeout propagates from SynthesizerFn all the way to
    // SpeechSynthesizer::synthesize().
    SpeechSynthesizer s("hello", valid_config(),
        [](const TtsConfig&, std::span<const std::string>)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return edge_tts::common::Result<std::vector<TtsChunk>>::fail(
                Error{ErrorCode::timeout, "WebSocket receive timed out"});
        });

    auto r = s.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::timeout);
}

TEST(Timeout, SavePropagatesReceiveTimeout) {
    // Timeout from the synthesizer path must surface through save() too.
    SpeechSynthesizer s("hello", valid_config(),
        [](const TtsConfig&, std::span<const std::string>)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return edge_tts::common::Result<std::vector<TtsChunk>>::fail(
                Error{ErrorCode::timeout, "WebSocket receive timed out"});
        });

    const fs::path mp = fs::temp_directory_path() / "cancel_test_timeout.mp3";
    auto r = s.save(mp);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::timeout);
    fs::remove(mp);
}

TEST(Timeout, ConnectTimeoutPropagatesThroughSession) {
    FakeWebSocketClient fake;
    fake.set_connect_error(Error{ErrorCode::timeout, "WebSocket connect timed out"});

    auto session = make_session(fake);
    const std::vector<std::string> chunks{"hello"};
    auto r = session.synthesize(valid_config(), chunks);

    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::timeout);
}

// ---------------------------------------------------------------------------
// Cancellation: cancel before synthesis starts
// ---------------------------------------------------------------------------

TEST(Cancellation, CancelBeforeSynthesizeReturnsCancel) {
    // cancel() before synthesize() must return ErrorCode::cancelled immediately
    // without invoking the synthesizer function.
    bool synthesizer_called = false;
    SpeechSynthesizer s("hello", valid_config(),
        [&synthesizer_called](const TtsConfig&, std::span<const std::string>)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            synthesizer_called = true;
            AudioChunk ac;
            ac.data = {std::byte{0x01}};
            return edge_tts::common::Result<std::vector<TtsChunk>>::ok({TtsChunk{std::move(ac)}});
        });

    s.cancel();

    auto r = s.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::cancelled);
    EXPECT_FALSE(synthesizer_called);
}

TEST(Cancellation, CancelBeforeSaveReturnsCancel) {
    bool synthesizer_called = false;
    SpeechSynthesizer s("hello", valid_config(),
        [&synthesizer_called](const TtsConfig&, std::span<const std::string>)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            synthesizer_called = true;
            return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
        });

    s.cancel();

    const fs::path mp = fs::temp_directory_path() / "cancel_test_before_save.mp3";
    auto r = s.save(mp);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::cancelled);
    EXPECT_FALSE(synthesizer_called);
    fs::remove(mp);
}

// ---------------------------------------------------------------------------
// Cancellation: cancel in the SynthesisSession receive loop
// (tests the within-chunk check point)
// ---------------------------------------------------------------------------

TEST(Cancellation, CancelDuringReceiveLoopViaToken) {
    // Build a real SynthesisSession with a shared CancellationToken.
    // The FakeWebSocketClient will return a cancelled error from receive(),
    // simulating what happens when cancel() fires while blocked.
    CancellationToken token;
    FakeWebSocketClient fake;
    fake.set_receive_error(Error{ErrorCode::cancelled, "synthesis was cancelled"});

    auto session = make_session(fake, token);
    const std::vector<std::string> chunks{"hello"};
    auto r = session.synthesize(valid_config(), chunks);

    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::cancelled);
}

TEST(Cancellation, CancelBeforeChunkViaToken) {
    // Set the token before synthesize() starts.  The between-chunk check point
    // in SynthesisSession::synthesize() must fire before even connecting.
    CancellationToken token;
    FakeWebSocketClient fake;
    push_session(fake);  // queued but should never be consumed

    token.cancel();  // cancel before the session starts

    auto session = make_session(fake, token);
    const std::vector<std::string> chunks{"hello"};
    auto r = session.synthesize(valid_config(), chunks);

    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::cancelled);
    // The connect should never have been attempted.
    EXPECT_EQ(fake.connect_count(), 0);
}

// ---------------------------------------------------------------------------
// Cancellation: cancel() is idempotent
// ---------------------------------------------------------------------------

TEST(Cancellation, CancelIsIdempotent) {
    SpeechSynthesizer s("hello", valid_config(),
        [](const TtsConfig&, std::span<const std::string>)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
        });

    // Multiple cancel() calls must not crash or corrupt state.
    s.cancel();
    s.cancel();
    s.cancel();

    auto r = s.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::cancelled);
}

// ---------------------------------------------------------------------------
// Cancellation: CancellationToken is independent per SpeechSynthesizer object
// ---------------------------------------------------------------------------

TEST(Cancellation, TokenIsNotSharedAcrossObjects) {
    // Cancelling one SpeechSynthesizer must not affect a fresh one.
    SpeechSynthesizer s1("hello", valid_config(),
        [](const TtsConfig&, std::span<const std::string>)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
        });
    s1.cancel();
    (void)s1.synthesize();  // consumed (returns cancelled)

    // A brand-new SpeechSynthesizer must be unaffected.
    bool ran = false;
    SpeechSynthesizer s2("hello", valid_config(),
        [&ran](const TtsConfig&, std::span<const std::string>)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            ran = true;
            return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
        });

    auto r = s2.synthesize();
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(ran);
}

// ---------------------------------------------------------------------------
// CancellationToken: shared semantics
// ---------------------------------------------------------------------------

TEST(CancellationToken, DefaultNotCancelled) {
    CancellationToken tok;
    EXPECT_FALSE(tok.is_cancelled());
}

TEST(CancellationToken, CancelSetsFlag) {
    CancellationToken tok;
    tok.cancel();
    EXPECT_TRUE(tok.is_cancelled());
}

TEST(CancellationToken, CopiesShareFlag) {
    CancellationToken original;
    CancellationToken copy = original;  // shares the same underlying flag

    original.cancel();
    EXPECT_TRUE(copy.is_cancelled());
}

TEST(CancellationToken, CancelOnCopySeenByOriginal) {
    CancellationToken original;
    CancellationToken copy = original;

    copy.cancel();
    EXPECT_TRUE(original.is_cancelled());
}

TEST(CancellationToken, IsCancelledReturnsFalseBeforeCancel) {
    CancellationToken tok;
    EXPECT_FALSE(tok.is_cancelled());
    tok.cancel();
    EXPECT_TRUE(tok.is_cancelled());
}

// ---------------------------------------------------------------------------
// ErrorCode::cancelled has correct string representation
// ---------------------------------------------------------------------------

TEST(CancellationToken, CancelledErrorCodeHasCorrectString) {
    EXPECT_EQ(edge_tts::common::to_string(ErrorCode::cancelled), "cancelled");
}

TEST(CancellationToken, CancelledErrorWhatContainsCancelledCode) {
    const Error e{ErrorCode::cancelled, "synthesis was cancelled"};
    const std::string w = e.what();
    EXPECT_NE(w.find("cancelled"), std::string::npos);
}
