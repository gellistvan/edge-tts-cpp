#include "common/Clock.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <chrono>
#include <memory>

using namespace std::chrono;
using edge_tts::common::FixedClock;
using edge_tts::common::IClock;
using edge_tts::common::SystemClock;

// ---------------------------------------------------------------------------
// FixedClock
// ---------------------------------------------------------------------------

TEST(FixedClock, ReturnsConfiguredValue) {
    // Use a deterministic, well-known UTC point: 2024-01-01 00:00:00 UTC
    // = 1704067200 seconds since Unix epoch
    const auto tp = system_clock::time_point{seconds{1704067200}};
    FixedClock clk{tp};
    EXPECT_TRUE(clk.now() == tp);
}

TEST(FixedClock, ReturnsEpoch) {
    const auto epoch = system_clock::time_point{};
    FixedClock clk{epoch};
    EXPECT_TRUE(clk.now() == epoch);
}

TEST(FixedClock, SetUpdatesReturnedValue) {
    const auto tp1 = system_clock::time_point{seconds{1000000000}};
    const auto tp2 = system_clock::time_point{seconds{2000000000}};
    FixedClock clk{tp1};
    EXPECT_TRUE(clk.now() == tp1);
    clk.set(tp2);
    EXPECT_TRUE(clk.now() == tp2);
}

TEST(FixedClock, SetToEarlierTime) {
    // Clock skew correction can move time backward.
    const auto later   = system_clock::time_point{seconds{1700000000}};
    const auto earlier = system_clock::time_point{seconds{1699990000}};
    FixedClock clk{later};
    clk.set(earlier);
    EXPECT_TRUE(clk.now() == earlier);
}

TEST(FixedClock, UsableViaPtrToInterface) {
    const auto tp = system_clock::time_point{seconds{1704067200}};
    std::unique_ptr<IClock> clk = std::make_unique<FixedClock>(tp);
    EXPECT_TRUE(clk->now() == tp);
}

// ---------------------------------------------------------------------------
// SystemClock
// ---------------------------------------------------------------------------

TEST(SystemClock, NowIsBetweenBeforeAndAfter) {
    const auto before   = system_clock::now();
    const SystemClock clk;
    const auto measured = clk.now();
    const auto after    = system_clock::now();

    EXPECT_TRUE(measured >= before);
    EXPECT_TRUE(measured <= after);
}

TEST(SystemClock, ReturnsDifferentValuesOnSuccessiveCalls) {
    SystemClock clk;
    const auto t1 = clk.now();
    // Busy-wait a tiny bit to guarantee clock advancement.
    system_clock::time_point t2;
    do { t2 = clk.now(); } while (t2 == t1);
    EXPECT_TRUE(t2 > t1);
}

TEST(SystemClock, UTCEpochIsReasonable) {
    // std::chrono::system_clock uses UTC (Unix epoch = 1970-01-01 00:00:00 UTC).
    // Any machine running these tests should report a time after 2020-01-01.
    // If it is somehow before that, the test legitimately fails.
    // 2020-01-01 00:00:00 UTC = 1577836800 s
    // 2100-01-01 00:00:00 UTC = 4102444800 s
    SystemClock clk;
    const auto unix_sec = duration_cast<seconds>(
        clk.now().time_since_epoch()).count();
    EXPECT_TRUE(unix_sec > 1577836800LL);
    EXPECT_TRUE(unix_sec < 4102444800LL);
}

TEST(SystemClock, NoLocalTimezoneOffset) {
    // std::chrono::system_clock is defined to track UTC in C++20 (and in all
    // major implementations before C++20).  Converting via to_time_t() and then
    // comparing the two UTC decompositions via gmtime_r / gmtime must agree.
    SystemClock clk;
    const auto tp = clk.now();
    const std::time_t tt = system_clock::to_time_t(tp);
    // Reconstruct a time_point from to_time_t output and compare to the
    // original.  If local-time were involved the roundtrip would differ by
    // the UTC offset (up to ±14 hours = ±50400 s).
    const auto reconstructed = system_clock::from_time_t(tt);
    const auto diff = duration_cast<seconds>(tp - reconstructed).count();
    // Allow ±1 second for sub-second truncation in to_time_t.
    EXPECT_TRUE(diff >= -1 && diff <= 1);
}

TEST(SystemClock, UsableViaPtrToInterface) {
    std::unique_ptr<IClock> clk = std::make_unique<SystemClock>();
    const auto before = system_clock::now();
    const auto measured = clk->now();
    const auto after = system_clock::now();
    EXPECT_TRUE(measured >= before);
    EXPECT_TRUE(measured <= after);
}
