#include "communication/EdgeTokenProvider.hpp"
#include "communication/EdgeServiceConfig.hpp"
#include "common/Clock.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <chrono>
#include <cctype>
#include <string>

using edge_tts::communication::EdgeTokenProvider;
using edge_tts::communication::default_edge_service_config;
using edge_tts::common::FixedClock;

// Helper: build a FixedClock from a Unix timestamp (seconds since 1970).
static FixedClock make_clock(long long unix_seconds)
{
    using namespace std::chrono;
    return FixedClock{system_clock::time_point{seconds{unix_seconds}}};
}

// ---------------------------------------------------------------------------
// Fixed-clock deterministic tests
//
// All expected tokens computed by running the reference Python drm.py algorithm:
//   ticks = unix + WIN_EPOCH
//   ticks -= ticks % 300
//   ticks *= 1e9 / 100
//   hashlib.sha256(f"{ticks:.0f}6A5AA1D4EAFF4E9FB37E23D68491D6F4"
//                  .encode("ascii")).hexdigest().upper()
//
// Verified independently with: echo -n "<str_to_hash>" | sha256sum | tr a-z A-Z
// ---------------------------------------------------------------------------

TEST(EdgeTokenProvider, FixedClockUnix0MatchesPythonReference) {
    // unix=0, str_to_hash="1164447360000000006A5AA1D4EAFF4E9FB37E23D68491D6F4"
    const auto clock = make_clock(0);
    const EdgeTokenProvider tp{default_edge_service_config(), clock};

    const auto r = tp.sec_ms_gec();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(),
        "7ECB79D14E3AA576D2D79E6D487A1388156D91E614B1BE11C64226A29BC8DD8C");
}

TEST(EdgeTokenProvider, FixedClockUnix1000000000MatchesPythonReference) {
    // unix=1000000000, str_to_hash="1264447350000000006A5AA1D4EAFF4E9FB37E23D68491D6F4"
    const auto clock = make_clock(1000000000LL);
    const EdgeTokenProvider tp{default_edge_service_config(), clock};

    const auto r = tp.sec_ms_gec();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(),
        "6594DCF2D741A251B0EDFB71C0034EBFEBF6D413CC1EA5D1B23E60B118A2F0E1");
}

TEST(EdgeTokenProvider, FixedClockUnix1700000000MatchesPythonReference) {
    // unix=1700000000, str_to_hash="1334447340000000006A5AA1D4EAFF4E9FB37E23D68491D6F4"
    const auto clock = make_clock(1700000000LL);
    const EdgeTokenProvider tp{default_edge_service_config(), clock};

    const auto r = tp.sec_ms_gec();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(),
        "42301B335578FEFDAE2637DED1ABD614505D432559EC08032B82048483726AFF");
}

// ---------------------------------------------------------------------------
// Token format
// ---------------------------------------------------------------------------

TEST(EdgeTokenProvider, TokenLength) {
    const auto clock = make_clock(0);
    const EdgeTokenProvider tp{default_edge_service_config(), clock};
    const auto r = tp.sec_ms_gec();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 64u);
}

TEST(EdgeTokenProvider, TokenIsUppercaseHex) {
    const auto clock = make_clock(1700000000LL);
    const EdgeTokenProvider tp{default_edge_service_config(), clock};
    const auto r = tp.sec_ms_gec();
    EXPECT_TRUE(r.has_value());
    for (const char c : r.value()) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
        EXPECT_TRUE(ok);
    }
}

// ---------------------------------------------------------------------------
// sec_ms_gec_version
// ---------------------------------------------------------------------------

TEST(EdgeTokenProvider, VersionMatchesReference) {
    const auto clock = make_clock(0);
    const EdgeTokenProvider tp{default_edge_service_config(), clock};
    // Reference: constants.py SEC_MS_GEC_VERSION = "1-143.0.3650.75"
    EXPECT_EQ(tp.sec_ms_gec_version(), "1-143.0.3650.75");
}

TEST(EdgeTokenProvider, VersionMatchesConfig) {
    const auto clock = make_clock(0);
    const auto cfg = default_edge_service_config();
    const EdgeTokenProvider tp{cfg, clock};
    EXPECT_EQ(tp.sec_ms_gec_version(), cfg.sec_ms_gec_version);
}

// ---------------------------------------------------------------------------
// Bucket boundary behaviour
// Timestamps within the same 300-second window produce the same token.
// Timestamps in different windows produce different tokens.
// Reference: ticks -= ticks % 300  (round down to 5-minute boundary)
// ---------------------------------------------------------------------------

TEST(EdgeTokenProvider, SameBucketSameToken) {
    // unix=1700000100 and unix=1700000299 fall in the same 300s bucket
    // (both round down to Windows ticks value for 1700000100)
    const auto clock_a = make_clock(1700000100LL);
    const auto clock_b = make_clock(1700000299LL);
    const EdgeTokenProvider tp_a{default_edge_service_config(), clock_a};
    const EdgeTokenProvider tp_b{default_edge_service_config(), clock_b};

    const auto ra = tp_a.sec_ms_gec();
    const auto rb = tp_b.sec_ms_gec();
    EXPECT_TRUE(ra.has_value());
    EXPECT_TRUE(rb.has_value());
    EXPECT_EQ(ra.value(), rb.value());
    // Exact value confirmed by Python reference
    EXPECT_EQ(ra.value(),
        "AE4CF72E466874182A75878E20EADA83D29A1C12CAD9C3E0E014CCE0BFA55880");
}

TEST(EdgeTokenProvider, DifferentBucketsDifferentTokens) {
    // unix=1700000000 is in a different bucket from unix=1700000100
    const auto clock_a = make_clock(1700000000LL);
    const auto clock_b = make_clock(1700000100LL);
    const EdgeTokenProvider tp_a{default_edge_service_config(), clock_a};
    const EdgeTokenProvider tp_b{default_edge_service_config(), clock_b};

    const auto ra = tp_a.sec_ms_gec();
    const auto rb = tp_b.sec_ms_gec();
    EXPECT_TRUE(ra.has_value());
    EXPECT_TRUE(rb.has_value());
    EXPECT_NE(ra.value(), rb.value());
}

// ---------------------------------------------------------------------------
// Clock independence — to_srt does not use wall clock
// ---------------------------------------------------------------------------

TEST(EdgeTokenProvider, NoWallClockDependency) {
    // Two providers with the SAME FixedClock produce identical tokens.
    const auto clock = make_clock(1000000000LL);
    const EdgeTokenProvider tp1{default_edge_service_config(), clock};
    const EdgeTokenProvider tp2{default_edge_service_config(), clock};

    const auto r1 = tp1.sec_ms_gec();
    const auto r2 = tp2.sec_ms_gec();
    EXPECT_TRUE(r1.has_value());
    EXPECT_TRUE(r2.has_value());
    EXPECT_EQ(r1.value(), r2.value());
}

// ---------------------------------------------------------------------------
// Algorithm step verification — boundary rounding
// unix=300 is a exact multiple of 300, so ticks += WIN_EPOCH maps to
// 11644473900 which is still a multiple of 300 → no change from rounding.
// ---------------------------------------------------------------------------

TEST(EdgeTokenProvider, ExactBucketBoundaryToken) {
    // unix=300, expected: str_to_hash="1164447390000000006A5AA1D4EAFF4E9FB37E23D68491D6F4"
    const auto clock = make_clock(300LL);
    const EdgeTokenProvider tp{default_edge_service_config(), clock};
    const auto r = tp.sec_ms_gec();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(),
        "ED93F5AAE06C01D88654A1831CAC424F7BE4878E9D5850F7F66DDCBDD7ED95B9");
}
