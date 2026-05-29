#include "edge_tts/subtitles/SubMaker.hpp"
#include "edge_tts/subtitles/SubtitleCue.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "edge_tts/common/Error.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <cstdint>
#include <string>

using edge_tts::subtitles::SubMaker;
using edge_tts::subtitles::SubtitleCue;
using edge_tts::core::BoundaryChunk;
using edge_tts::core::BoundaryEventType;
using edge_tts::common::ErrorCode;

// Helper: build a BoundaryChunk for tests.
static BoundaryChunk make_chunk(
    BoundaryEventType type,
    std::int64_t      offset_ticks,
    std::int64_t      duration_ticks,
    const char*       text)
{
    BoundaryChunk c;
    c.type           = type;
    c.offset_ticks   = offset_ticks;
    c.duration_ticks = duration_ticks;
    c.text           = text;
    return c;
}

// Shorthand constants matching reference constants.py
static constexpr std::int64_t TICKS_PER_SECOND = 10'000'000;
static constexpr std::int64_t TICKS_PER_MS     = 10'000;

// ---------------------------------------------------------------------------
// Feed sentence boundary
// ---------------------------------------------------------------------------

TEST(SubMaker, FeedSentenceBoundary) {
    SubMaker sm;
    const auto chunk = make_chunk(
        BoundaryEventType::SentenceBoundary, 0, TICKS_PER_SECOND, "Hello, world.");

    const auto r = sm.feed(chunk);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(sm.cues().size(), 1u);
}

// ---------------------------------------------------------------------------
// Feed word boundary (reference supports both types)
// ---------------------------------------------------------------------------

TEST(SubMaker, FeedWordBoundary) {
    SubMaker sm;
    const auto chunk = make_chunk(
        BoundaryEventType::WordBoundary, 0, 500 * TICKS_PER_MS, "Hello");

    const auto r = sm.feed(chunk);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(sm.cues().size(), 1u);
}

// ---------------------------------------------------------------------------
// Multiple boundaries of the same type
// ---------------------------------------------------------------------------

TEST(SubMaker, MultipleSametype) {
    SubMaker sm;
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary, 0, TICKS_PER_SECOND, "One"));
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary, 2 * TICKS_PER_SECOND, TICKS_PER_SECOND, "Two"));
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary, 4 * TICKS_PER_SECOND, TICKS_PER_SECOND, "Three"));

    EXPECT_EQ(sm.cues().size(), 3u);
}

// ---------------------------------------------------------------------------
// Mixed types are rejected (reference: ValueError on type mismatch)
// ---------------------------------------------------------------------------

TEST(SubMaker, MixedTypeRejected) {
    SubMaker sm;
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary, 0, TICKS_PER_SECOND, "Sentence"));
    const auto r = sm.feed(make_chunk(BoundaryEventType::WordBoundary, TICKS_PER_SECOND, 500 * TICKS_PER_MS, "Word"));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_argument);
}

TEST(SubMaker, MixedTypeWordThenSentenceRejected) {
    SubMaker sm;
    (void)sm.feed(make_chunk(BoundaryEventType::WordBoundary, 0, 500 * TICKS_PER_MS, "Word"));
    const auto r = sm.feed(make_chunk(BoundaryEventType::SentenceBoundary, TICKS_PER_SECOND, TICKS_PER_SECOND, "Sentence"));
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_argument);
}

// ---------------------------------------------------------------------------
// Cue start/end calculation
//   start = offset_ticks / 10_000  (ms, integer truncation)
//   end   = (offset_ticks + duration_ticks) / 10_000
// ---------------------------------------------------------------------------

TEST(SubMaker, CueStartEndCalculation) {
    SubMaker sm;
    // offset = 500ms = 5,000,000 ticks; duration = 1000ms = 10,000,000 ticks
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary,
                       500 * TICKS_PER_MS, 1000 * TICKS_PER_MS, "Hello"));

    const auto cues = sm.cues();
    EXPECT_EQ(cues.size(), 1u);
    EXPECT_EQ(cues[0].start.milliseconds(), 500);
    EXPECT_EQ(cues[0].end.milliseconds(),   1500);
}

TEST(SubMaker, StartEqualsOffsetDividedByTicksPerMs) {
    SubMaker sm;
    // offset = 12345ms exactly
    (void)sm.feed(make_chunk(BoundaryEventType::WordBoundary,
                       12345 * TICKS_PER_MS, 100 * TICKS_PER_MS, "X"));
    const auto cues = sm.cues();
    EXPECT_EQ(cues[0].start.milliseconds(), 12345);
    EXPECT_EQ(cues[0].end.milliseconds(),   12445);
}

// ---------------------------------------------------------------------------
// Text is stored verbatim (already XML-unescaped by MetadataJsonParser)
// ---------------------------------------------------------------------------

TEST(SubMaker, TextStoredVerbatim) {
    SubMaker sm;
    // Text "A & B" arrives already unescaped (was "&amp;" on the wire)
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary, 0, TICKS_PER_SECOND, "A & B"));
    EXPECT_EQ(sm.cues()[0].text, "A & B");
}

TEST(SubMaker, TextWithUnicodeStoredVerbatim) {
    SubMaker sm;
    (void)sm.feed(make_chunk(BoundaryEventType::WordBoundary, 0, TICKS_PER_SECOND, "你好世界"));
    EXPECT_EQ(sm.cues()[0].text, "你好世界");
}

// ---------------------------------------------------------------------------
// Zero duration — cue is created but SrtComposer will skip it (start >= end)
// ---------------------------------------------------------------------------

TEST(SubMaker, ZeroDurationCueCreated) {
    SubMaker sm;
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary, TICKS_PER_SECOND, 0, "Zero"));
    EXPECT_EQ(sm.cues().size(), 1u);
    // start == end
    const auto cues = sm.cues(); // store before indexing to avoid dangling ref
    EXPECT_EQ(cues[0].start.milliseconds(), cues[0].end.milliseconds());
}

TEST(SubMaker, ZeroDurationSkippedInSrt) {
    SubMaker sm;
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary, TICKS_PER_SECOND, 0, "Zero"));
    const auto r = sm.to_srt();
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty()); // SrtComposer skips start >= end
}

// ---------------------------------------------------------------------------
// to_srt() output — matches SrtComposer format exactly
// ---------------------------------------------------------------------------

TEST(SubMaker, ToSrtSingleCue) {
    SubMaker sm;
    // 0ms start, 1000ms end → "00:00:00,000 --> 00:00:01,000"
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary,
                       0, 1000 * TICKS_PER_MS, "Hello."));

    const auto r = sm.to_srt();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(),
        "1\n"
        "00:00:00,000 --> 00:00:01,000\n"
        "Hello.\n"
        "\n");
}

TEST(SubMaker, ToSrtTwoCues) {
    SubMaker sm;
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary,
                       0, 1000 * TICKS_PER_MS, "First."));
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary,
                       2000 * TICKS_PER_MS, 1000 * TICKS_PER_MS, "Second."));

    const auto r = sm.to_srt();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(),
        "1\n"
        "00:00:00,000 --> 00:00:01,000\n"
        "First.\n"
        "\n"
        "2\n"
        "00:00:02,000 --> 00:00:03,000\n"
        "Second.\n"
        "\n");
}

// ---------------------------------------------------------------------------
// to_srt() does NOT reset state (reference: get_srt() is idempotent)
// ---------------------------------------------------------------------------

TEST(SubMaker, ToSrtDoesNotResetState) {
    SubMaker sm;
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary,
                       0, TICKS_PER_SECOND, "Hello."));

    const auto r1 = sm.to_srt();
    const auto r2 = sm.to_srt();
    EXPECT_TRUE(r1.has_value());
    EXPECT_TRUE(r2.has_value());
    EXPECT_EQ(r1.value(), r2.value()); // same output both times
    EXPECT_EQ(sm.cues().size(), 1u);  // cues still present
}

// ---------------------------------------------------------------------------
// feed() after to_srt() — continues accumulating
// ---------------------------------------------------------------------------

TEST(SubMaker, FeedAfterToSrt) {
    SubMaker sm;
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary,
                       0, TICKS_PER_SECOND, "First."));

    const auto r1 = sm.to_srt();
    EXPECT_EQ(sm.cues().size(), 1u);

    // Feed a second cue after to_srt()
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary,
                       2000 * TICKS_PER_MS, TICKS_PER_SECOND, "Second."));
    EXPECT_EQ(sm.cues().size(), 2u);

    const auto r2 = sm.to_srt();
    EXPECT_TRUE(r2.has_value());
    EXPECT_NE(r2.value().find("Second."), std::string::npos);
}

// ---------------------------------------------------------------------------
// clear() resets state — cues and type lock
// ---------------------------------------------------------------------------

TEST(SubMaker, ClearResetsCues) {
    SubMaker sm;
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary, 0, TICKS_PER_SECOND, "A"));
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary, 2 * TICKS_PER_SECOND, TICKS_PER_SECOND, "B"));
    EXPECT_EQ(sm.cues().size(), 2u);

    sm.clear();
    EXPECT_EQ(sm.cues().size(), 0u);
}

TEST(SubMaker, ClearResetsTypeLock) {
    SubMaker sm;
    // Lock to SentenceBoundary
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary, 0, TICKS_PER_SECOND, "Sent"));
    // Type mismatch would fail here
    EXPECT_FALSE(sm.feed(make_chunk(BoundaryEventType::WordBoundary, 0, TICKS_PER_SECOND, "Word")).has_value());

    sm.clear();

    // After clear, WordBoundary is now allowed
    const auto r = sm.feed(make_chunk(BoundaryEventType::WordBoundary, 0, 500 * TICKS_PER_MS, "Word"));
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(sm.cues().size(), 1u);
}

TEST(SubMaker, ClearThenToSrtReturnsEmpty) {
    SubMaker sm;
    (void)sm.feed(make_chunk(BoundaryEventType::SentenceBoundary, 0, TICKS_PER_SECOND, "Hi"));
    sm.clear();
    const auto r = sm.to_srt();
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

// ---------------------------------------------------------------------------
// Empty SubMaker
// ---------------------------------------------------------------------------

TEST(SubMaker, EmptyToSrtIsEmpty) {
    SubMaker sm;
    const auto r = sm.to_srt();
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

TEST(SubMaker, EmptyCuesIsEmpty) {
    SubMaker sm;
    EXPECT_TRUE(sm.cues().empty());
}
