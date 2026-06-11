// API-level timeout and cancellation edge case tests.
//
// Coverage gaps addressed here (beyond CancellationTests.cpp):
//
//   SpeechSynthesizer::synthesize():
//     - send_text() timeout propagates to the caller
//     - send_text() cancelled propagates to the caller
//     - connect() cancelled propagates to the caller
//
//   SpeechSynthesizer::save():
//     - send_text() timeout propagates through save()
//     - connect() cancelled propagates through save()
//
//   SpeechSynthesizer::synthesize_stream():
//     - timeout from the synthesizer fn is delivered via on_error
//     - cancelled from the synthesizer fn is delivered via on_error
//     - on_error receives the exact ErrorCode (no code conversion)
//
//   Cancelled vs. timeout — observable distinction at the API boundary:
//     - synthesize() result codes for cancelled and timeout differ
//     - SpeechSynthesizer::cancel() produces ErrorCode::cancelled (not timeout)
//     - a timeout error is never reported as cancelled and vice-versa
//
// All tests use the SynthesizerFn injection constructor or FakeWebSocketClient
// through TestWire — no real network, no sleeps.

#include "api/SpeechSynthesizer.hpp"
#include "api/StreamCallbacks.hpp"
#include "api/SynthesisOptions.hpp"
#include "communication/ConnectionMetadata.hpp"
#include "communication/EdgeProtocol.hpp"
#include "communication/EdgeServiceConfig.hpp"
#include "communication/EdgeTokenProvider.hpp"
#include "communication/FakeWebSocketClient.hpp"
#include "communication/SynthesisSession.hpp"
#include "common/CancellationToken.hpp"
#include "common/Error.hpp"
#include "common/Result.hpp"
#include "core/Chunk.hpp"
#include "core/TtsConfig.hpp"
#include "support/WebSocketFrameHelpers.hpp"
#include "vendor/minigtest/minigtest.hpp"
#include "ApiTestFixtures.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

using edge_tts::api::SpeechSynthesizer;
using edge_tts::api::StreamCallbacks;
using edge_tts::api::SynthesizerFn;
using edge_tts::common::CancellationToken;
using edge_tts::common::Error;
using edge_tts::common::ErrorCode;
using edge_tts::communication::FakeWebSocketClient;
using edge_tts::communication::SynthesisSession;
using edge_tts::core::TtsChunk;
using edge_tts::core::TtsConfig;
using edge_tts::test::make_seam;
using edge_tts::test::push_session;
using edge_tts::test::TestWire;
using edge_tts::test::valid_config;

namespace fs = std::filesystem;

// Helper: return a SynthesizerFn that injects the given error unconditionally.
static SynthesizerFn inject_error(ErrorCode code, const char* msg) {
    return [code, msg](const TtsConfig&, std::span<const std::string>)
               -> edge_tts::common::Result<std::vector<TtsChunk>>
    {
        return edge_tts::common::Result<std::vector<TtsChunk>>::fail(
            Error{code, msg});
    };
}

// ---------------------------------------------------------------------------
// synthesize() — send_text() timeout / cancelled propagation
// ---------------------------------------------------------------------------

TEST(TimeoutEdgeCase, SendTimeoutPropagatesThroughSynthesize) {
    // Timeout from send_text() must surface unchanged as ErrorCode::timeout.
    TestWire w;
    FakeWebSocketClient ws;
    ws.set_send_error(Error{ErrorCode::timeout, "send timed out"});
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer s("hello", valid_config(), make_seam(session));
    auto r = s.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::timeout);
}

TEST(TimeoutEdgeCase, SendCancelledPropagatesThroughSynthesize) {
    // Cancelled from send_text() must propagate as cancelled, not network_error.
    TestWire w;
    FakeWebSocketClient ws;
    ws.set_send_error(Error{ErrorCode::cancelled, "send cancelled"});
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer s("hello", valid_config(), make_seam(session));
    auto r = s.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::cancelled);
}

TEST(TimeoutEdgeCase, ConnectCancelledPropagatesThroughSynthesize) {
    TestWire w;
    FakeWebSocketClient ws;
    ws.set_connect_error(Error{ErrorCode::cancelled, "connect cancelled"});
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer s("hello", valid_config(), make_seam(session));
    auto r = s.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::cancelled);
}

TEST(TimeoutEdgeCase, ConnectTimeoutPropagatesThroughSynthesize) {
    TestWire w;
    FakeWebSocketClient ws;
    ws.set_connect_error(Error{ErrorCode::timeout, "connect timeout"});
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    SpeechSynthesizer s("hello", valid_config(), make_seam(session));
    auto r = s.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::timeout);
}

// ---------------------------------------------------------------------------
// save() — timeout / cancelled propagation
// ---------------------------------------------------------------------------

TEST(TimeoutEdgeCase, SendTimeoutPropagatesThroughSave) {
    TestWire w;
    FakeWebSocketClient ws;
    ws.set_send_error(Error{ErrorCode::timeout, "send timed out"});
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    const fs::path mp = fs::temp_directory_path() / "tc_send_timeout_save.mp3";
    SpeechSynthesizer s("hello", valid_config(), make_seam(session));
    auto r = s.save(mp);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::timeout);
    fs::remove(mp);
}

TEST(TimeoutEdgeCase, ConnectCancelledPropagatesThroughSave) {
    TestWire w;
    FakeWebSocketClient ws;
    ws.set_connect_error(Error{ErrorCode::cancelled, "connect cancelled"});
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    const fs::path mp = fs::temp_directory_path() / "tc_connect_cancel_save.mp3";
    SpeechSynthesizer s("hello", valid_config(), make_seam(session));
    auto r = s.save(mp);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::cancelled);
    fs::remove(mp);
}

TEST(TimeoutEdgeCase, ReceiveCancelledPropagatesThroughSave) {
    TestWire w;
    FakeWebSocketClient ws;
    ws.set_receive_error(Error{ErrorCode::cancelled, "receive cancelled"});
    SynthesisSession session{ws, w.protocol, w.svc, w.tokens, w.meta, w.clock};

    const fs::path mp = fs::temp_directory_path() / "tc_recv_cancel_save.mp3";
    SpeechSynthesizer s("hello", valid_config(), make_seam(session));
    auto r = s.save(mp);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::cancelled);
    fs::remove(mp);
}

// ---------------------------------------------------------------------------
// synthesize_stream() — timeout / cancelled delivered via on_error
// ---------------------------------------------------------------------------

TEST(TimeoutEdgeCase, TimeoutDeliveredViaOnErrorInStream) {
    SpeechSynthesizer s("hello", valid_config(),
        inject_error(ErrorCode::timeout, "receive timeout"));

    ErrorCode cb_code = ErrorCode::none;
    int error_count   = 0;
    int complete_count = 0;
    StreamCallbacks cbs;
    cbs.on_complete = [&]() { ++complete_count; };
    cbs.on_error    = [&](const Error& e) { cb_code = e.code(); ++error_count; };

    auto r = s.synthesize_stream(std::move(cbs));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::timeout);
    EXPECT_EQ(cb_code, ErrorCode::timeout);
    EXPECT_EQ(error_count, 1);
    EXPECT_EQ(complete_count, 0);
}

TEST(TimeoutEdgeCase, CancelledDeliveredViaOnErrorInStream) {
    SpeechSynthesizer s("hello", valid_config(),
        inject_error(ErrorCode::cancelled, "cancelled"));

    ErrorCode cb_code = ErrorCode::none;
    StreamCallbacks cbs;
    cbs.on_error = [&](const Error& e) { cb_code = e.code(); };

    auto r = s.synthesize_stream(std::move(cbs));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::cancelled);
    EXPECT_EQ(cb_code, ErrorCode::cancelled);
}

TEST(TimeoutEdgeCase, NetworkErrorDeliveredViaOnErrorInStream) {
    SpeechSynthesizer s("hello", valid_config(),
        inject_error(ErrorCode::network_error, "dropped"));

    ErrorCode cb_code = ErrorCode::none;
    StreamCallbacks cbs;
    cbs.on_error = [&](const Error& e) { cb_code = e.code(); };

    auto r = s.synthesize_stream(std::move(cbs));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::network_error);
    EXPECT_EQ(cb_code, ErrorCode::network_error);
}

// ---------------------------------------------------------------------------
// Cancelled vs. timeout — observable distinction at the API boundary
// ---------------------------------------------------------------------------

TEST(CancelledVsTimeout, SpeechSynthesizerCancelProducesCancelledNotTimeout) {
    // SpeechSynthesizer::cancel() must produce ErrorCode::cancelled,
    // never ErrorCode::timeout.
    SpeechSynthesizer s("hello", valid_config(),
        [](const TtsConfig&, std::span<const std::string>)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
        });

    s.cancel();
    auto r = s.synthesize();

    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::cancelled);
    EXPECT_NE(r.error().code(), ErrorCode::timeout);
}

TEST(CancelledVsTimeout, TimeoutErrorIsNotCancelled) {
    SpeechSynthesizer s("hello", valid_config(),
        inject_error(ErrorCode::timeout, "receive timed out"));

    auto r = s.synthesize();

    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::timeout);
    EXPECT_NE(r.error().code(), ErrorCode::cancelled);
}

TEST(CancelledVsTimeout, CancelledErrorIsNotTimeout) {
    SpeechSynthesizer s("hello", valid_config(),
        inject_error(ErrorCode::cancelled, "cancelled"));

    auto r = s.synthesize();

    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::cancelled);
    EXPECT_NE(r.error().code(), ErrorCode::timeout);
}

TEST(CancelledVsTimeout, StreamCallbacksDistinguishCancelledFromTimeout) {
    // A stream consumer can branch on code() to distinguish the two conditions.
    bool saw_timeout   = false;
    bool saw_cancelled = false;

    {
        SpeechSynthesizer s("hello", valid_config(),
            inject_error(ErrorCode::timeout, "timeout"));
        StreamCallbacks cbs;
        cbs.on_error = [&](const Error& e) {
            if (e.code() == ErrorCode::timeout)   saw_timeout   = true;
            if (e.code() == ErrorCode::cancelled) saw_cancelled = true;
        };
        (void)s.synthesize_stream(std::move(cbs));
    }

    EXPECT_TRUE(saw_timeout);
    EXPECT_FALSE(saw_cancelled);

    saw_timeout = saw_cancelled = false;

    {
        SpeechSynthesizer s2("hello", valid_config(),
            inject_error(ErrorCode::cancelled, "cancelled"));
        StreamCallbacks cbs;
        cbs.on_error = [&](const Error& e) {
            if (e.code() == ErrorCode::timeout)   saw_timeout   = true;
            if (e.code() == ErrorCode::cancelled) saw_cancelled = true;
        };
        (void)s2.synthesize_stream(std::move(cbs));
    }

    EXPECT_FALSE(saw_timeout);
    EXPECT_TRUE(saw_cancelled);
}

// ---------------------------------------------------------------------------
// Cancellation before and during stream — complementary to StreamingApiTests
// ---------------------------------------------------------------------------

TEST(CancelledVsTimeout, SpeechSynthesizerCancelFromStreamProducesCancelled) {
    // When cancel() fires within synthesize_stream() (simulated via a
    // pre-cancelled token), on_error must see ErrorCode::cancelled.
    SpeechSynthesizer s("hello", valid_config(),
        inject_error(ErrorCode::cancelled, "cancelled from transport"));

    ErrorCode cb_code = ErrorCode::none;
    StreamCallbacks cbs;
    cbs.on_error = [&](const Error& e) { cb_code = e.code(); };

    auto r = s.synthesize_stream(std::move(cbs));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::cancelled);
    EXPECT_EQ(cb_code, ErrorCode::cancelled);
    EXPECT_NE(cb_code, ErrorCode::timeout);
}

TEST(CancelledVsTimeout, ExplicitCancelBeforeStreamGivesCancelledNotTimeout) {
    // cancel() before synthesize_stream() → ErrorCode::cancelled, not timeout.
    SpeechSynthesizer s("hello", valid_config(),
        [](const TtsConfig&, std::span<const std::string>)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
        });

    s.cancel();

    ErrorCode cb_code = ErrorCode::none;
    StreamCallbacks cbs;
    cbs.on_error = [&](const Error& e) { cb_code = e.code(); };

    auto r = s.synthesize_stream(std::move(cbs));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::cancelled);
    EXPECT_EQ(cb_code, ErrorCode::cancelled);
    EXPECT_NE(cb_code, ErrorCode::timeout);
}
