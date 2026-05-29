#include "edge_tts/subtitles/SubtitleTime.hpp"
#include "edge_tts/common/Error.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <cstdint>
#include <string>

using edge_tts::subtitles::SubtitleTime;
using edge_tts::common::ErrorCode;

// Tick constant: 1 second = 10,000,000 ticks (100 ns units, TICKS_PER_SECOND)
static constexpr std::int64_t TICKS_PER_SECOND = 10'000'000;
// Derived: 1 ms = 10,000 ticks
static constexpr std::int64_t TICKS_PER_MS = 10'000;

// ---------------------------------------------------------------------------
// Zero
// ---------------------------------------------------------------------------

TEST(SubtitleTime, Zero) {
    const auto r = SubtitleTime::from_edge_ticks(0);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().milliseconds(), 0);
    EXPECT_EQ(r.value().to_srt_timestamp(), "00:00:00,000");
}

// ---------------------------------------------------------------------------
// One millisecond equivalent (10,000 ticks)
// ---------------------------------------------------------------------------

TEST(SubtitleTime, OneMillisecond) {
    const auto r = SubtitleTime::from_edge_ticks(TICKS_PER_MS);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().milliseconds(), 1);
    EXPECT_EQ(r.value().to_srt_timestamp(), "00:00:00,001");
}

// ---------------------------------------------------------------------------
// One second (10,000,000 ticks)
// ---------------------------------------------------------------------------

TEST(SubtitleTime, OneSecond) {
    const auto r = SubtitleTime::from_edge_ticks(TICKS_PER_SECOND);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().milliseconds(), 1000);
    EXPECT_EQ(r.value().to_srt_timestamp(), "00:00:01,000");
}

// ---------------------------------------------------------------------------
// One minute (60 * 10,000,000 ticks)
// ---------------------------------------------------------------------------

TEST(SubtitleTime, OneMinute) {
    const auto r = SubtitleTime::from_edge_ticks(60 * TICKS_PER_SECOND);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().milliseconds(), 60'000);
    EXPECT_EQ(r.value().to_srt_timestamp(), "00:01:00,000");
}

// ---------------------------------------------------------------------------
// One hour (3600 * 10,000,000 ticks)
// ---------------------------------------------------------------------------

TEST(SubtitleTime, OneHour) {
    const auto r = SubtitleTime::from_edge_ticks(3600 * TICKS_PER_SECOND);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().milliseconds(), 3'600'000);
    EXPECT_EQ(r.value().to_srt_timestamp(), "01:00:00,000");
}

// ---------------------------------------------------------------------------
// Mixed timestamp: 1h 23m 4s 0ms → "01:23:04,000"
// (Matches the doctest in srt_composer.py timedelta_to_srt_timestamp)
// ---------------------------------------------------------------------------

TEST(SubtitleTime, MixedHourMinSecMs) {
    // 1h 23m 4s = 4984 seconds = 4,984,000 ms = 49,840,000,000 ticks
    const std::int64_t ticks = (1 * 3600 + 23 * 60 + 4) * TICKS_PER_SECOND;
    const auto r = SubtitleTime::from_edge_ticks(ticks);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().to_srt_timestamp(), "01:23:04,000");
}

TEST(SubtitleTime, MixedWithMilliseconds) {
    // 0h 0m 1s 500ms
    const std::int64_t ticks = TICKS_PER_SECOND + 500 * TICKS_PER_MS;
    const auto r = SubtitleTime::from_edge_ticks(ticks);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().milliseconds(), 1500);
    EXPECT_EQ(r.value().to_srt_timestamp(), "00:00:01,500");
}

TEST(SubtitleTime, AllComponentsNonZero) {
    // 2h 15m 33s 750ms
    const std::int64_t ms = (2 * 3'600'000) + (15 * 60'000) + (33 * 1'000) + 750;
    const std::int64_t ticks = ms * TICKS_PER_MS;
    const auto r = SubtitleTime::from_edge_ticks(ticks);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().to_srt_timestamp(), "02:15:33,750");
}

// ---------------------------------------------------------------------------
// Truncation/rounding edge
//
// C++ uses integer truncation: ms = ticks / 10_000.
// Python uses float division ticks/10 → timedelta (banker's rounding to µs)
// → floor-divide by 1000 for ms.  A difference of 1 ms is possible only when
// ticks % 10_000 ≥ 9_995 (fractional µs ≥ 0.5 and rounds the µs up across a
// ms boundary).  Document and test the truncation behavior.
// ---------------------------------------------------------------------------

TEST(SubtitleTime, TruncationBelowMillisecond) {
    // 9999 ticks = 0.9999 ms → truncated to 0 ms
    // Python: 9999/10 = 999.9 µs → rounds to 1000 µs → 1 ms (different!)
    // C++ integer: 9999 / 10000 = 0 ms  (truncation documented behavior)
    const auto r = SubtitleTime::from_edge_ticks(9999);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().milliseconds(), 0);
    EXPECT_EQ(r.value().to_srt_timestamp(), "00:00:00,000");
}

TEST(SubtitleTime, ExactMillisecondBoundary) {
    // 10000 ticks = 1.0000 ms → 1 ms (no ambiguity)
    const auto r = SubtitleTime::from_edge_ticks(10'000);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().milliseconds(), 1);
}

TEST(SubtitleTime, JustBelowSecondBoundary) {
    // 9999999 ticks = 999.9999 ms → truncated to 999 ms
    const auto r = SubtitleTime::from_edge_ticks(9'999'999);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().milliseconds(), 999);
    EXPECT_EQ(r.value().to_srt_timestamp(), "00:00:00,999");
}

// ---------------------------------------------------------------------------
// Negative ticks → error
// ---------------------------------------------------------------------------

TEST(SubtitleTime, NegativeTicksRejected) {
    const auto r = SubtitleTime::from_edge_ticks(-1);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_argument);
}

TEST(SubtitleTime, LargeNegativeTicksRejected) {
    const auto r = SubtitleTime::from_edge_ticks(-1'000'000'000LL);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_argument);
}

// ---------------------------------------------------------------------------
// Very large ticks (hours > 99)
// ---------------------------------------------------------------------------

TEST(SubtitleTime, VeryLargeTicks) {
    // 1000 hours exactly
    const std::int64_t ticks = 1000LL * 3600 * TICKS_PER_SECOND;
    const auto r = SubtitleTime::from_edge_ticks(ticks);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().milliseconds(), 1000LL * 3'600'000);
    EXPECT_EQ(r.value().to_srt_timestamp(), "1000:00:00,000");
}

TEST(SubtitleTime, LargeWithAllComponents) {
    // 100h 5m 30s 250ms
    const std::int64_t ms = (100LL * 3'600'000) + (5 * 60'000) + (30 * 1'000) + 250;
    const auto r = SubtitleTime::from_edge_ticks(ms * TICKS_PER_MS);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().to_srt_timestamp(), "100:05:30,250");
}

// ---------------------------------------------------------------------------
// Milliseconds() accessor
// ---------------------------------------------------------------------------

TEST(SubtitleTime, MillisecondsAccessor) {
    const auto r = SubtitleTime::from_edge_ticks(12'345 * TICKS_PER_MS);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().milliseconds(), 12'345);
}

// ---------------------------------------------------------------------------
// SRT format: comma not dot, zero-padded components
// ---------------------------------------------------------------------------

TEST(SubtitleTime, SrtUsesCommaNotDot) {
    const auto r = SubtitleTime::from_edge_ticks(1500 * TICKS_PER_MS);
    EXPECT_TRUE(r.has_value());
    const std::string ts = r.value().to_srt_timestamp();
    // Ensure comma separator (not dot)
    EXPECT_NE(ts.find(','), std::string::npos);
    EXPECT_EQ(ts.find('.'), std::string::npos);
    EXPECT_EQ(ts, "00:00:01,500");
}

TEST(SubtitleTime, SrtZeroPaddedComponents) {
    // 1h 2m 3s 4ms
    const std::int64_t ms = (1 * 3'600'000) + (2 * 60'000) + (3 * 1'000) + 4;
    const auto r = SubtitleTime::from_edge_ticks(ms * TICKS_PER_MS);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().to_srt_timestamp(), "01:02:03,004");
}
