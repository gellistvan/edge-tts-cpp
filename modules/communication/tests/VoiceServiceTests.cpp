#include "communication/VoiceService.hpp"
#include "communication/EdgeServiceConfig.hpp"
#include "communication/EdgeTokenProvider.hpp"
#include "communication/FakeHttpClient.hpp"
#include "core/Voice.hpp"
#include "serialization/VoiceJsonParser.hpp"
#include "common/Clock.hpp"
#include "common/Error.hpp"
#include "common/IdGenerator.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <chrono>
#include <string>
#include <vector>

using edge_tts::communication::VoiceFilter;
using edge_tts::communication::VoiceService;
using edge_tts::communication::EdgeTokenProvider;
using edge_tts::communication::FakeHttpClient;
using edge_tts::communication::default_edge_service_config;
using edge_tts::core::VoiceGender;
using edge_tts::serialization::VoiceJsonParser;
using edge_tts::common::ErrorCode;
using edge_tts::common::FixedClock;
using edge_tts::common::IdGenerator;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const VoiceJsonParser k_parser{};
static const auto k_cfg = default_edge_service_config();
static IdGenerator k_ids{};

// Fixed clock at a known UTC timestamp for deterministic token generation.
// 2024-01-01 00:00:00 UTC (Unix time 1704067200)
static FixedClock k_clock{
    std::chrono::system_clock::from_time_t(1704067200)
};
static EdgeTokenProvider k_tokens{k_cfg, k_clock};

// Minimal valid JSON for a single voice.
static std::string one_voice_json(
    const char* short_name,
    const char* locale,
    const char* gender = "Female")
{
    return std::string{R"json([{"Name":"N","ShortName":")json"}
        + short_name
        + R"json(","Gender":")json" + gender
        + R"json(","Locale":")json" + locale
        + R"json(","SuggestedCodec":"mp3","FriendlyName":"F","Status":"GA","VoiceTag":{"ContentCategories":[],"VoicePersonalities":[]}}])json";
}

static std::string two_voices_json() {
    return R"json([
      {"Name":"N1","ShortName":"en-US-EmmaMultilingualNeural","Gender":"Female","Locale":"en-US","SuggestedCodec":"mp3","FriendlyName":"Emma","Status":"GA","VoiceTag":{"ContentCategories":[],"VoicePersonalities":[]}},
      {"Name":"N2","ShortName":"zh-CN-XiaoxiaoNeural","Gender":"Female","Locale":"zh-CN","SuggestedCodec":"mp3","FriendlyName":"Xiaoxiao","Status":"GA","VoiceTag":{"ContentCategories":[],"VoicePersonalities":[]}}
    ])json";
}

// Multi-voice JSON in deliberately non-alphabetical ShortName order.
static std::string wire_order_json() {
    return R"json([
      {"Name":"N3","ShortName":"zh-CN-XiaoxiaoNeural","Gender":"Female","Locale":"zh-CN","SuggestedCodec":"mp3","FriendlyName":"F3","Status":"GA","VoiceTag":{"ContentCategories":[],"VoicePersonalities":[]}},
      {"Name":"N1","ShortName":"en-US-EmmaMultilingualNeural","Gender":"Female","Locale":"en-US","SuggestedCodec":"mp3","FriendlyName":"F1","Status":"GA","VoiceTag":{"ContentCategories":[],"VoicePersonalities":[]}},
      {"Name":"N2","ShortName":"de-DE-KatjaNeural","Gender":"Female","Locale":"de-DE","SuggestedCodec":"mp3","FriendlyName":"F2","Status":"GA","VoiceTag":{"ContentCategories":[],"VoicePersonalities":[]}}
    ])json";
}

// ---------------------------------------------------------------------------
// Sends GET to voices endpoint
// ---------------------------------------------------------------------------

TEST(VoiceService, SendsGetMethod) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    (void)svc.list_voices();
    EXPECT_EQ(http.last_request()->method, "GET");
}

TEST(VoiceService, SendsToVoicesEndpoint) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    (void)svc.list_voices();
    // URL must start with the configured voices_endpoint base.
    EXPECT_EQ(http.last_request()->url.substr(0, k_cfg.voices_endpoint.size()),
              k_cfg.voices_endpoint);
}

TEST(VoiceService, UrlContainsTrustedClientToken) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    (void)svc.list_voices();
    EXPECT_NE(http.last_request()->url.find(k_cfg.trusted_client_token), std::string::npos);
}

// ---------------------------------------------------------------------------
// DRM token appended to URL
// ---------------------------------------------------------------------------

TEST(VoiceService, UrlContainsSecMsGecParam) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    (void)svc.list_voices();
    EXPECT_NE(http.last_request()->url.find("Sec-MS-GEC="), std::string::npos);
}

TEST(VoiceService, UrlContainsSecMsGecVersionParam) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    (void)svc.list_voices();
    EXPECT_NE(http.last_request()->url.find("Sec-MS-GEC-Version="), std::string::npos);
}

TEST(VoiceService, SecMsGecVersionMatchesConfig) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    (void)svc.list_voices();
    EXPECT_NE(http.last_request()->url.find(k_cfg.sec_ms_gec_version), std::string::npos);
}

TEST(VoiceService, SecMsGecTokenIs64UpperHex) {
    // The Sec-MS-GEC token is a SHA-256 digest: 64 uppercase hex chars.
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    IdGenerator ids;
    FixedClock  clock{std::chrono::system_clock::from_time_t(1704067200)};
    EdgeTokenProvider tokens{k_cfg, clock};
    VoiceService svc{k_cfg, http, k_parser, ids, tokens};
    (void)svc.list_voices();

    const std::string& url = http.last_request()->url;
    const std::string needle = "Sec-MS-GEC=";
    const auto pos = url.find(needle);
    EXPECT_NE(pos, std::string::npos);
    if (pos == std::string::npos) return;

    const auto token_start = pos + needle.size();
    // Token ends at next '&' or end of string.
    const auto amp = url.find('&', token_start);
    const std::string token = (amp == std::string::npos)
        ? url.substr(token_start)
        : url.substr(token_start, amp - token_start);

    EXPECT_EQ(token.size(), 64u);
    for (char c : token) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'));
    }
}

// ---------------------------------------------------------------------------
// Includes required headers
// ---------------------------------------------------------------------------

TEST(VoiceService, SetsUserAgentHeader) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    (void)svc.list_voices();
    EXPECT_EQ(http.last_request()->headers.at("User-Agent"), k_cfg.user_agent);
}

TEST(VoiceService, SetsAcceptHeader) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    (void)svc.list_voices();
    // Accept header
    EXPECT_EQ(http.last_request()->headers.at("Accept"), "*/*");
}

TEST(VoiceService, SetsAcceptLanguageHeader) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    (void)svc.list_voices();
    EXPECT_EQ(http.last_request()->headers.at("Accept-Language"), "en-US,en;q=0.9");
}

TEST(VoiceService, SetsAcceptEncodingHeader) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    (void)svc.list_voices();
    // Accept-Encoding header
    EXPECT_EQ(http.last_request()->headers.at("Accept-Encoding"), "gzip, deflate, br, zstd");
}

TEST(VoiceService, SetsCookieMuidHeader) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    (void)svc.list_voices();
    // Cookie header: muid=<32-upper-hex>;
    const auto& cookie = http.last_request()->headers.at("Cookie");
    EXPECT_EQ(cookie.substr(0, 5), "muid=");
    EXPECT_EQ(cookie.back(), ';');
    // muid= (5) + 32 hex chars + ; (1) = 38
    EXPECT_EQ(cookie.size(), 38u);
    // All hex chars must be uppercase
    for (std::size_t i = 5; i < 37; ++i) {
        const char c = cookie[i];
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'));
    }
}

TEST(VoiceService, FreshMuidPerRequest) {
    // A new MUID is generated on every request.
    // Two consecutive list_voices() calls must produce different MUID cookies.
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    IdGenerator fresh_ids;
    VoiceService svc{k_cfg, http, k_parser, fresh_ids, k_tokens};

    (void)svc.list_voices();
    const std::string first_cookie = http.last_request()->headers.at("Cookie");

    (void)svc.list_voices();
    const std::string second_cookie = http.last_request()->headers.at("Cookie");

    EXPECT_NE(first_cookie, second_cookie);
}

// ---------------------------------------------------------------------------
// Parses successful JSON
// ---------------------------------------------------------------------------

TEST(VoiceService, ParsesSuccessfulJson) {
    FakeHttpClient http;
    http.set_response({200, {}, one_voice_json("en-US-EmmaMultilingualNeural", "en-US")});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};

    const auto r = svc.list_voices();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 1u);
    EXPECT_EQ(r.value()[0].short_name, "en-US-EmmaMultilingualNeural");
    EXPECT_EQ(r.value()[0].locale,     "en-US");
}

TEST(VoiceService, EmptyArrayReturnsEmptyVector) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    const auto r = svc.list_voices();
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

// ---------------------------------------------------------------------------
// Non-2xx returns service error
// ---------------------------------------------------------------------------

TEST(VoiceService, Http403ReturnsServiceError) {
    // Two consecutive 403s: first attempt and retry both fail → service_error.
    FakeHttpClient http;
    http.push_response({403, {}, "Forbidden"});
    http.push_response({403, {}, "Forbidden"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    const auto r = svc.list_voices();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::service_error);
}

TEST(VoiceService, Http500ReturnsServiceError) {
    FakeHttpClient http;
    http.set_response({500, {}, "Internal Server Error"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    const auto r = svc.list_voices();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::service_error);
}

TEST(VoiceService, Non200StatusCodeInErrorContext) {
    FakeHttpClient http;
    http.set_response({503, {}, ""});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    const auto r = svc.list_voices();
    EXPECT_FALSE(r.has_value());
    // Context should contain the status code
    EXPECT_NE(r.error().context().find("503"), std::string_view::npos);
}

// ---------------------------------------------------------------------------
// 403 retry — drm_error from HTTP 403 triggers one retry with corrected token
// ---------------------------------------------------------------------------

TEST(VoiceService, Http403FollowedBy200Succeeds) {
    // First request gets 403 (DRM token rejected); retry gets 200 → success.
    // list_voices() retries exactly once on HTTP 403.
    FakeHttpClient http;
    http.push_response({403, {}, "Forbidden"});                          // first attempt
    http.push_response({200, {}, one_voice_json("en-US-X", "en-US")});  // retry
    IdGenerator ids;
    FixedClock  clock{std::chrono::system_clock::from_time_t(1704067200)};
    EdgeTokenProvider tokens{k_cfg, clock};
    VoiceService svc{k_cfg, http, k_parser, ids, tokens};

    const auto r = svc.list_voices();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 1u);
    EXPECT_EQ(http.send_count(), 2);
}

TEST(VoiceService, Http403RetrySendsExactlyTwoRequests) {
    FakeHttpClient http;
    http.push_response({403, {}, "Forbidden"});
    http.push_response({403, {}, "Forbidden"});
    IdGenerator ids;
    FixedClock  clock{std::chrono::system_clock::from_time_t(1704067200)};
    EdgeTokenProvider tokens{k_cfg, clock};
    VoiceService svc{k_cfg, http, k_parser, ids, tokens};

    (void)svc.list_voices();
    EXPECT_EQ(http.send_count(), 2);
}

TEST(VoiceService, Http500DoesNotRetry) {
    // 500 is not a DRM error; only 403 triggers a retry.
    FakeHttpClient http;
    http.set_response({500, {}, "Server Error"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    (void)svc.list_voices();
    EXPECT_EQ(http.send_count(), 1);
}

// ---------------------------------------------------------------------------
// 403 clock-skew correction via Date header
// ---------------------------------------------------------------------------

TEST(VoiceService, Http403DateHeaderAdjustsSkewFromServerTime) {
    // Server Date is 10 minutes ahead of local clock → skew = 600 s.
    // Local clock: 2024-01-01 00:00:00 UTC = Unix 1704067200
    // Server Date: 2024-01-01 00:10:00 UTC = Unix 1704067800
    FakeHttpClient http;
    http.push_response({403, {{"Date", "Mon, 01 Jan 2024 00:10:00 GMT"}}, "Forbidden"});
    http.push_response({403, {}, "Forbidden"});
    IdGenerator ids;
    FixedClock  clock{std::chrono::system_clock::from_time_t(1704067200)};
    EdgeTokenProvider tokens{k_cfg, clock};
    VoiceService svc{k_cfg, http, k_parser, ids, tokens};

    (void)svc.list_voices();
    EXPECT_EQ(tokens.clock_skew_seconds(), 600.0);
}

TEST(VoiceService, Http403DateHeaderRetrySucceeds) {
    // Date-header skew applied before retry; retry returns 200 → success.
    FakeHttpClient http;
    http.push_response({403, {{"Date", "Mon, 01 Jan 2024 00:10:00 GMT"}}, "Forbidden"});
    http.push_response({200, {}, one_voice_json("en-US-X", "en-US")});
    IdGenerator ids;
    FixedClock  clock{std::chrono::system_clock::from_time_t(1704067200)};
    EdgeTokenProvider tokens{k_cfg, clock};
    VoiceService svc{k_cfg, http, k_parser, ids, tokens};

    const auto r = svc.list_voices();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 1u);
    EXPECT_EQ(http.send_count(), 2);
}

TEST(VoiceService, Http403MissingDateHeaderUsesFallbackSkew) {
    // No Date header in the 403 response → fall back to 300 s fixed adjustment.
    FakeHttpClient http;
    http.push_response({403, {}, "Forbidden"});
    http.push_response({403, {}, "Forbidden"});
    IdGenerator ids;
    FixedClock  clock{std::chrono::system_clock::from_time_t(1704067200)};
    EdgeTokenProvider tokens{k_cfg, clock};
    VoiceService svc{k_cfg, http, k_parser, ids, tokens};

    (void)svc.list_voices();
    EXPECT_EQ(tokens.clock_skew_seconds(), 300.0);
}

TEST(VoiceService, Http403InvalidDateHeaderUsesFallbackSkew) {
    // Unparsable Date header → fall back to 300 s fixed adjustment.
    FakeHttpClient http;
    http.push_response({403, {{"Date", "not-a-date"}}, "Forbidden"});
    http.push_response({403, {}, "Forbidden"});
    IdGenerator ids;
    FixedClock  clock{std::chrono::system_clock::from_time_t(1704067200)};
    EdgeTokenProvider tokens{k_cfg, clock};
    VoiceService svc{k_cfg, http, k_parser, ids, tokens};

    (void)svc.list_voices();
    EXPECT_EQ(tokens.clock_skew_seconds(), 300.0);
}

// ---------------------------------------------------------------------------
// HTTP error propagates
// ---------------------------------------------------------------------------

TEST(VoiceService, HttpNetworkErrorPropagates) {
    FakeHttpClient http;
    http.set_error({ErrorCode::network_error, "connection refused"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    const auto r = svc.list_voices();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::network_error);
}

TEST(VoiceService, HttpTimeoutPropagates) {
    FakeHttpClient http;
    http.set_error({ErrorCode::timeout, "read timeout"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    const auto r = svc.list_voices();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::timeout);
}

// ---------------------------------------------------------------------------
// Parse error propagates
// ---------------------------------------------------------------------------

TEST(VoiceService, MalformedJsonPropagatesParseError) {
    FakeHttpClient http;
    http.set_response({200, {}, "not valid json {{{"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    const auto r = svc.list_voices();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

TEST(VoiceService, MissingRequiredFieldPropagatesParseError) {
    // Missing "Gender" field
    FakeHttpClient http;
    http.set_response({200, {},
        R"json([{"Name":"N","ShortName":"en-US-X","Locale":"en-US","SuggestedCodec":"mp3","FriendlyName":"F","Status":"GA"}])json"});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};
    const auto r = svc.list_voices();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

// ---------------------------------------------------------------------------
// Filter by locale
// ---------------------------------------------------------------------------

TEST(VoiceService, FilterByLocale) {
    FakeHttpClient http;
    http.set_response({200, {}, two_voices_json()});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};

    VoiceFilter filter;
    filter.locale = "zh-CN";
    const auto r = svc.list_voices(filter);

    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 1u);
    EXPECT_EQ(r.value()[0].short_name, "zh-CN-XiaoxiaoNeural");
}

TEST(VoiceService, FilterByLocaleNoMatch) {
    FakeHttpClient http;
    http.set_response({200, {}, two_voices_json()});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};

    VoiceFilter filter;
    filter.locale = "ja-JP";
    const auto r = svc.list_voices(filter);

    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

// ---------------------------------------------------------------------------
// Filter by gender
// ---------------------------------------------------------------------------

TEST(VoiceService, FilterByGenderFemale) {
    FakeHttpClient http;
    // Mix genders
    const std::string json = R"json([
      {"Name":"N1","ShortName":"en-US-EmmaMultilingualNeural","Gender":"Female","Locale":"en-US","SuggestedCodec":"mp3","FriendlyName":"F1","Status":"GA","VoiceTag":{"ContentCategories":[],"VoicePersonalities":[]}},
      {"Name":"N2","ShortName":"en-GB-RyanNeural","Gender":"Male","Locale":"en-GB","SuggestedCodec":"mp3","FriendlyName":"F2","Status":"GA","VoiceTag":{"ContentCategories":[],"VoicePersonalities":[]}}
    ])json";
    http.set_response({200, {}, json});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};

    VoiceFilter filter;
    filter.gender = VoiceGender::female;
    const auto r = svc.list_voices(filter);

    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 1u);
    EXPECT_EQ(r.value()[0].gender, VoiceGender::female);
}

TEST(VoiceService, FilterByGenderMale) {
    FakeHttpClient http;
    const std::string json = R"json([
      {"Name":"N1","ShortName":"en-US-EmmaMultilingualNeural","Gender":"Female","Locale":"en-US","SuggestedCodec":"mp3","FriendlyName":"F1","Status":"GA","VoiceTag":{"ContentCategories":[],"VoicePersonalities":[]}},
      {"Name":"N2","ShortName":"en-GB-RyanNeural","Gender":"Male","Locale":"en-GB","SuggestedCodec":"mp3","FriendlyName":"F2","Status":"GA","VoiceTag":{"ContentCategories":[],"VoicePersonalities":[]}}
    ])json";
    http.set_response({200, {}, json});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};

    VoiceFilter filter;
    filter.gender = VoiceGender::male;
    const auto r = svc.list_voices(filter);

    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 1u);
    EXPECT_EQ(r.value()[0].gender, VoiceGender::male);
}

// ---------------------------------------------------------------------------
// Filter by short_name
// ---------------------------------------------------------------------------

TEST(VoiceService, FilterByShortName) {
    FakeHttpClient http;
    http.set_response({200, {}, two_voices_json()});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};

    VoiceFilter filter;
    filter.short_name = "en-US-EmmaMultilingualNeural";
    const auto r = svc.list_voices(filter);

    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 1u);
    EXPECT_EQ(r.value()[0].short_name, "en-US-EmmaMultilingualNeural");
}

// ---------------------------------------------------------------------------
// Combined filter
// ---------------------------------------------------------------------------

TEST(VoiceService, FilterByLocaleAndGender) {
    FakeHttpClient http;
    const std::string json = R"json([
      {"Name":"N1","ShortName":"en-US-EmmaMultilingualNeural","Gender":"Female","Locale":"en-US","SuggestedCodec":"mp3","FriendlyName":"F1","Status":"GA","VoiceTag":{"ContentCategories":[],"VoicePersonalities":[]}},
      {"Name":"N2","ShortName":"en-US-GuyNeural","Gender":"Male","Locale":"en-US","SuggestedCodec":"mp3","FriendlyName":"F2","Status":"GA","VoiceTag":{"ContentCategories":[],"VoicePersonalities":[]}},
      {"Name":"N3","ShortName":"zh-CN-XiaoxiaoNeural","Gender":"Female","Locale":"zh-CN","SuggestedCodec":"mp3","FriendlyName":"F3","Status":"GA","VoiceTag":{"ContentCategories":[],"VoicePersonalities":[]}}
    ])json";
    http.set_response({200, {}, json});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};

    VoiceFilter filter;
    filter.locale = "en-US";
    filter.gender = VoiceGender::female;
    const auto r = svc.list_voices(filter);

    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 1u);
    EXPECT_EQ(r.value()[0].short_name, "en-US-EmmaMultilingualNeural");
}

// ---------------------------------------------------------------------------
// Ordering: list_voices() returns voices in wire order.
// Sorting (by ShortName) is done only by the CLI display layer.
// ---------------------------------------------------------------------------

TEST(VoiceService, WireOrderPreserved) {
    FakeHttpClient http;
    http.set_response({200, {}, wire_order_json()});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};

    const auto r = svc.list_voices();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 3u);
    // Wire order: zh-CN-XiaoxiaoNeural, en-US-EmmaMultilingualNeural, de-DE-KatjaNeural
    // (not alphabetically sorted)
    EXPECT_EQ(r.value()[0].short_name, "zh-CN-XiaoxiaoNeural");
    EXPECT_EQ(r.value()[1].short_name, "en-US-EmmaMultilingualNeural");
    EXPECT_EQ(r.value()[2].short_name, "de-DE-KatjaNeural");
}

TEST(VoiceService, NoImplicitSortingApplied) {
    // Verify that list_voices() does NOT sort alphabetically by ShortName.
    // The reference sorts only in _print_voices() for CLI display.
    FakeHttpClient http;
    http.set_response({200, {}, wire_order_json()});
    VoiceService svc{k_cfg, http, k_parser, k_ids, k_tokens};

    const auto r = svc.list_voices();
    EXPECT_TRUE(r.has_value());
    // If sorted alphabetically: de-DE-KatjaNeural, en-US-..., zh-CN-...
    // Wire order: zh-CN-..., en-US-..., de-DE-...  (NOT alphabetical)
    EXPECT_EQ(r.value()[0].short_name, "zh-CN-XiaoxiaoNeural"); // not "de-DE-KatjaNeural"
}
