// Tests for SpeechSynthesizer::synthesize_stream() — the progressive callback API.
//
// Acceptance criteria verified here:
//   - Compile-only public API consumer pattern (no internal headers required).
//   - on_audio receives all audio chunks in order.
//   - on_boundary receives all boundary chunks in order.
//   - on_complete fires exactly once on success; on_error does not fire.
//   - on_error fires exactly once on synthesis failure; on_complete does not fire.
//   - Cancellation via cancel() halts dispatch and fires on_error(cancelled).
//   - cancel() called from within on_audio halts after the current chunk.
//   - Single-use guard: second call returns ErrorCode::invalid_state.
//   - Null callbacks are silently skipped (no crash).
//   - Return value mirrors completion status.
//
// All tests use the SynthesizerFn injection constructor — no real network.

#include "api/SpeechSynthesizer.hpp"
#include "api/StreamCallbacks.hpp"
#include "common/Error.hpp"
#include "common/Result.hpp"
#include "core/Chunk.hpp"
#include "core/TtsConfig.hpp"
#include "vendor/minigtest/minigtest.hpp"
#include "ApiTestFixtures.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

using edge_tts::api::SpeechSynthesizer;
using edge_tts::api::StreamCallbacks;
using edge_tts::api::SynthesizerFn;
using edge_tts::common::Error;
using edge_tts::common::ErrorCode;
using edge_tts::core::AudioChunk;
using edge_tts::core::BoundaryChunk;
using edge_tts::core::BoundaryEventType;
using edge_tts::core::TtsChunk;
using edge_tts::core::TtsConfig;
using edge_tts::test::make_fake;
using edge_tts::test::valid_config;

// ---------------------------------------------------------------------------
// Compile-only: verify public API is usable with only public headers.
// This test body deliberately instantiates each callback type.
// ---------------------------------------------------------------------------

TEST(StreamingApi, CompileOnlyPublicApiPattern) {
    // A downstream library user would write exactly this pattern.
    // No internal headers (communication/, serialization/, etc.) are needed.
    std::vector<std::byte> collected_audio;
    std::vector<BoundaryChunk> collected_boundaries;
    bool completed = false;

    SpeechSynthesizer s("hello", valid_config(), make_fake());

    StreamCallbacks cbs;
    cbs.on_audio = [&](std::span<const std::byte> data) {
        collected_audio.insert(collected_audio.end(), data.begin(), data.end());
    };
    cbs.on_boundary = [&](const BoundaryChunk& b) {
        collected_boundaries.push_back(b);
    };
    cbs.on_complete = [&]() { completed = true; };
    cbs.on_error    = [](const Error&) {};

    auto r = s.synthesize_stream(std::move(cbs));
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(completed);
}

// ---------------------------------------------------------------------------
// Audio chunks delivered in order
// ---------------------------------------------------------------------------

TEST(StreamingApi, AudioChunksDeliveredInOrder) {
    AudioChunk a1; a1.data = {std::byte{0x01}, std::byte{0x02}};
    AudioChunk a2; a2.data = {std::byte{0x03}, std::byte{0x04}};
    AudioChunk a3; a3.data = {std::byte{0x05}};

    SpeechSynthesizer s("hello", valid_config(),
        make_fake({TtsChunk{a1}, TtsChunk{a2}, TtsChunk{a3}}));

    std::vector<std::vector<std::byte>> received;
    StreamCallbacks cbs;
    cbs.on_audio = [&](std::span<const std::byte> data) {
        received.emplace_back(data.begin(), data.end());
    };

    ASSERT_TRUE(s.synthesize_stream(std::move(cbs)).has_value());

    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[0], a1.data);
    EXPECT_EQ(received[1], a2.data);
    EXPECT_EQ(received[2], a3.data);
}

TEST(StreamingApi, AudioBytesMatchPayload) {
    AudioChunk ac;
    ac.data = {std::byte{'H'}, std::byte{'I'}};

    SpeechSynthesizer s("hello", valid_config(), make_fake({TtsChunk{ac}}));

    std::vector<std::byte> got;
    StreamCallbacks cbs;
    cbs.on_audio = [&](std::span<const std::byte> data) {
        got.insert(got.end(), data.begin(), data.end());
    };

    ASSERT_TRUE(s.synthesize_stream(std::move(cbs)).has_value());
    EXPECT_EQ(got, ac.data);
}

// ---------------------------------------------------------------------------
// Boundary chunks delivered
// ---------------------------------------------------------------------------

TEST(StreamingApi, BoundaryChunksDelivered) {
    BoundaryChunk b1;
    b1.type          = BoundaryEventType::WordBoundary;
    b1.text          = "hello";
    b1.offset_ticks  = 1'000'000;
    b1.duration_ticks= 500'000;

    BoundaryChunk b2;
    b2.type          = BoundaryEventType::SentenceBoundary;
    b2.text          = "world";
    b2.offset_ticks  = 2'000'000;
    b2.duration_ticks= 750'000;

    SpeechSynthesizer s("hello world", valid_config(),
        make_fake({TtsChunk{b1}, TtsChunk{b2}}));

    std::vector<BoundaryChunk> received;
    StreamCallbacks cbs;
    cbs.on_boundary = [&](const BoundaryChunk& b) { received.push_back(b); };

    ASSERT_TRUE(s.synthesize_stream(std::move(cbs)).has_value());

    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0], b1);
    EXPECT_EQ(received[1], b2);
}

TEST(StreamingApi, MixedChunksDeliveredInOrder) {
    AudioChunk    ac;  ac.data = {std::byte{0xAA}};
    BoundaryChunk bc;  bc.text = "word"; bc.offset_ticks = 100;

    // Service can interleave audio and boundary chunks.
    SpeechSynthesizer s("hello", valid_config(),
        make_fake({TtsChunk{ac}, TtsChunk{bc}, TtsChunk{ac}}));

    std::vector<std::string> order;
    StreamCallbacks cbs;
    cbs.on_audio    = [&](std::span<const std::byte>) { order.push_back("audio"); };
    cbs.on_boundary = [&](const BoundaryChunk&)       { order.push_back("boundary"); };

    ASSERT_TRUE(s.synthesize_stream(std::move(cbs)).has_value());

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], "audio");
    EXPECT_EQ(order[1], "boundary");
    EXPECT_EQ(order[2], "audio");
}

// ---------------------------------------------------------------------------
// on_complete fires on success; on_error does not
// ---------------------------------------------------------------------------

TEST(StreamingApi, OnCompleteFiresOnSuccess) {
    SpeechSynthesizer s("hello", valid_config(), make_fake());

    int complete_count = 0;
    int error_count    = 0;
    StreamCallbacks cbs;
    cbs.on_complete = [&]() { ++complete_count; };
    cbs.on_error    = [&](const Error&) { ++error_count; };

    ASSERT_TRUE(s.synthesize_stream(std::move(cbs)).has_value());
    EXPECT_EQ(complete_count, 1);
    EXPECT_EQ(error_count,    0);
}

TEST(StreamingApi, ReturnValueIsOkOnSuccess) {
    SpeechSynthesizer s("hello", valid_config(), make_fake());
    StreamCallbacks cbs;
    auto r = s.synthesize_stream(std::move(cbs));
    EXPECT_TRUE(r.has_value());
}

// ---------------------------------------------------------------------------
// on_error fires on synthesis failure
// ---------------------------------------------------------------------------

TEST(StreamingApi, OnErrorFiresWhenSynthesisFails) {
    SpeechSynthesizer s("hello", valid_config(),
        [](const TtsConfig&, std::span<const std::string>)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return edge_tts::common::Result<std::vector<TtsChunk>>::fail(
                Error{ErrorCode::network_error, "simulated failure"});
        });

    int error_count    = 0;
    int complete_count = 0;
    ErrorCode received_code = ErrorCode::none;

    StreamCallbacks cbs;
    cbs.on_complete = [&]() { ++complete_count; };
    cbs.on_error    = [&](const Error& e) {
        ++error_count;
        received_code = e.code();
    };

    auto r = s.synthesize_stream(std::move(cbs));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::network_error);
    EXPECT_EQ(error_count,    1);
    EXPECT_EQ(complete_count, 0);
    EXPECT_EQ(received_code,  ErrorCode::network_error);
}

TEST(StreamingApi, ReturnValueMatchesOnErrorCode) {
    SpeechSynthesizer s("hello", valid_config(),
        [](const TtsConfig&, std::span<const std::string>)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return edge_tts::common::Result<std::vector<TtsChunk>>::fail(
                Error{ErrorCode::timeout, "timed out"});
        });

    ErrorCode cb_code = ErrorCode::none;
    StreamCallbacks cbs;
    cbs.on_error = [&](const Error& e) { cb_code = e.code(); };

    auto r = s.synthesize_stream(std::move(cbs));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::timeout);
    EXPECT_EQ(cb_code, ErrorCode::timeout);
}

// ---------------------------------------------------------------------------
// Cancellation: cancel() before synthesize_stream
// ---------------------------------------------------------------------------

TEST(StreamingApi, CancelBeforeStreamReturnsCancel) {
    bool synthesizer_called = false;
    SpeechSynthesizer s("hello", valid_config(),
        [&synthesizer_called](const TtsConfig&, std::span<const std::string>)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            synthesizer_called = true;
            return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
        });

    s.cancel();

    ErrorCode cb_code = ErrorCode::none;
    StreamCallbacks cbs;
    cbs.on_error = [&](const Error& e) { cb_code = e.code(); };

    auto r = s.synthesize_stream(std::move(cbs));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::cancelled);
    EXPECT_EQ(cb_code, ErrorCode::cancelled);
    EXPECT_FALSE(synthesizer_called);
}

// ---------------------------------------------------------------------------
// Cancellation: cancel() from inside on_audio halts dispatch
// ---------------------------------------------------------------------------

TEST(StreamingApi, CancelFromWithinOnAudioHaltsDispatch) {
    // Three audio chunks.  cancel() is called from inside the first callback.
    // Only one on_audio call should occur; on_error fires with cancelled.
    AudioChunk a1; a1.data = {std::byte{0x01}};
    AudioChunk a2; a2.data = {std::byte{0x02}};
    AudioChunk a3; a3.data = {std::byte{0x03}};

    SpeechSynthesizer s("hello", valid_config(),
        make_fake({TtsChunk{a1}, TtsChunk{a2}, TtsChunk{a3}}));

    int audio_count    = 0;
    int complete_count = 0;
    int error_count    = 0;

    StreamCallbacks cbs;
    cbs.on_audio = [&](std::span<const std::byte>) {
        ++audio_count;
        s.cancel();  // cancel after receiving the first chunk
    };
    cbs.on_complete = [&]() { ++complete_count; };
    cbs.on_error    = [&](const Error& e) {
        ++error_count;
        EXPECT_EQ(e.code(), ErrorCode::cancelled);
    };

    auto r = s.synthesize_stream(std::move(cbs));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::cancelled);
    // First chunk was delivered; subsequent ones were suppressed.
    EXPECT_EQ(audio_count, 1);
    EXPECT_EQ(complete_count, 0);
    EXPECT_EQ(error_count, 1);
}

TEST(StreamingApi, CancelFromWithinOnBoundaryHaltsDispatch) {
    BoundaryChunk b1; b1.text = "first";  b1.offset_ticks = 100;
    BoundaryChunk b2; b2.text = "second"; b2.offset_ticks = 200;

    SpeechSynthesizer s("hello", valid_config(),
        make_fake({TtsChunk{b1}, TtsChunk{b2}}));

    int boundary_count = 0;
    StreamCallbacks cbs;
    cbs.on_boundary = [&](const BoundaryChunk&) {
        ++boundary_count;
        s.cancel();
    };
    cbs.on_error = [](const Error&) {};

    auto r = s.synthesize_stream(std::move(cbs));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::cancelled);
    EXPECT_EQ(boundary_count, 1);
}

// ---------------------------------------------------------------------------
// Single-use guard
// ---------------------------------------------------------------------------

TEST(StreamingApi, StreamIsSingleUse) {
    SpeechSynthesizer s("hello", valid_config(), make_fake());

    auto r1 = s.synthesize_stream({});
    EXPECT_TRUE(r1.has_value());

    auto r2 = s.synthesize_stream({});
    EXPECT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().code(), ErrorCode::invalid_state);
}

TEST(StreamingApi, StreamAfterSynthesizeIsBlocked) {
    SpeechSynthesizer s("hello", valid_config(), make_fake());
    (void)s.synthesize();

    auto r = s.synthesize_stream({});
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_state);
}

TEST(StreamingApi, SynthesizeAfterStreamIsBlocked) {
    SpeechSynthesizer s("hello", valid_config(), make_fake());
    (void)s.synthesize_stream({});

    auto r = s.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_state);
}

// ---------------------------------------------------------------------------
// Null callbacks are silently skipped — no crash or UB
// ---------------------------------------------------------------------------

TEST(StreamingApi, NullCallbacksAreSkipped) {
    AudioChunk ac; ac.data = {std::byte{0x42}};
    SpeechSynthesizer s("hello", valid_config(), make_fake({TtsChunk{ac}}));

    // All four callbacks are null — must complete without crashing.
    StreamCallbacks cbs;  // all members default-constructed (null)
    auto r = s.synthesize_stream(std::move(cbs));
    EXPECT_TRUE(r.has_value());
}

TEST(StreamingApi, PartialCallbacksAreSkipped) {
    // Only on_audio set; on_boundary, on_complete, on_error are null.
    AudioChunk ac; ac.data = {std::byte{0x01}};
    BoundaryChunk bc; bc.text = "word";

    SpeechSynthesizer s("hello", valid_config(),
        make_fake({TtsChunk{ac}, TtsChunk{bc}}));

    int audio_calls = 0;
    StreamCallbacks cbs;
    cbs.on_audio = [&](std::span<const std::byte>) { ++audio_calls; };

    auto r = s.synthesize_stream(std::move(cbs));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(audio_calls, 1);
}

// ---------------------------------------------------------------------------
// Empty text: no chunks, on_complete fires, return value is ok
// ---------------------------------------------------------------------------

TEST(StreamingApi, EmptyTextFiresOnComplete) {
    // TextChunker returns empty vector for whitespace-only text — no synthesis.
    SpeechSynthesizer s("   ", valid_config());

    int complete = 0;
    int audio    = 0;
    StreamCallbacks cbs;
    cbs.on_audio    = [&](std::span<const std::byte>) { ++audio; };
    cbs.on_complete = [&]() { ++complete; };

    auto r = s.synthesize_stream(std::move(cbs));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(audio,    0);
    EXPECT_EQ(complete, 1);
}
