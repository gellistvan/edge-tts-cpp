// Unit tests for parse_http_date().
// Format: "Wkd, DD Mon YYYY HH:MM:SS GMT" (RFC 2616)

#include "communication/HttpDate.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <cstdint>
#include <optional>

using edge_tts::communication::parse_http_date;

// ---------------------------------------------------------------------------
// Valid dates
// ---------------------------------------------------------------------------

TEST(ParseHttpDate, UnixEpochReturnsZero) {
    // 1970-01-01 00:00:00 UTC = Unix timestamp 0.
    auto r = parse_http_date("Thu, 01 Jan 1970 00:00:00 GMT");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, std::int64_t{0});
}

TEST(ParseHttpDate, KnownMidYearDate) {
    // 2021-06-15 12:34:56 UTC.
    // Computed: days_from_civil(2021,6,15) = 18793
    //   18793 * 86400 + 12*3600 + 34*60 + 56 = 1623760496
    auto r = parse_http_date("Tue, 15 Jun 2021 12:34:56 GMT");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, std::int64_t{1623760496});
}

TEST(ParseHttpDate, EndOfYear) {
    // 2023-12-31 23:59:59 UTC.
    // days_from_civil(2023,12,31) = 19722
    //   19722 * 86400 + 23*3600 + 59*60 + 59 = 1704067199
    auto r = parse_http_date("Sun, 31 Dec 2023 23:59:59 GMT");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, std::int64_t{1704067199});
}

TEST(ParseHttpDate, LeapDay) {
    // 2024-02-29 00:00:00 UTC (leap year).
    // 2024-01-01 = 19723 days, Jan has 31 days, Feb 1-28 = 28 days,
    // Feb 29 is the 60th day: 19723 + 59 = 19782 days from epoch
    //   19782 * 86400 = 1709164800
    auto r = parse_http_date("Thu, 29 Feb 2024 00:00:00 GMT");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, std::int64_t{1709164800});
}

TEST(ParseHttpDate, ThirtySecondsAfterEpoch) {
    // Used in DRM retry tests: epoch + 30 s.
    auto r = parse_http_date("Thu, 01 Jan 1970 00:00:30 GMT");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, std::int64_t{30});
}

// ---------------------------------------------------------------------------
// Invalid / malformed input
// ---------------------------------------------------------------------------

TEST(ParseHttpDate, EmptyStringReturnsNullopt) {
    EXPECT_FALSE(parse_http_date("").has_value());
}

TEST(ParseHttpDate, ArbitraryStringReturnsNullopt) {
    EXPECT_FALSE(parse_http_date("not a date").has_value());
}

TEST(ParseHttpDate, UnknownMonthReturnsNullopt) {
    EXPECT_FALSE(parse_http_date("Thu, 01 Xyz 1970 00:00:00 GMT").has_value());
}

TEST(ParseHttpDate, MissingTimezoneReturnsNullopt) {
    EXPECT_FALSE(parse_http_date("Thu, 01 Jan 1970 00:00:00").has_value());
}

TEST(ParseHttpDate, TruncatedStringReturnsNullopt) {
    EXPECT_FALSE(parse_http_date("Thu, 01").has_value());
}
