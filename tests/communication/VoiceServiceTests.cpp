#include "edge_tts/communication/VoiceService.hpp"
#include "edge_tts/communication/EdgeServiceConfig.hpp"
#include "edge_tts/communication/FakeHttpClient.hpp"
#include "edge_tts/core/Voice.hpp"
#include "edge_tts/serialization/VoiceJsonParser.hpp"
#include "edge_tts/common/Error.hpp"
#include "edge_tts/common/IdGenerator.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <string>
#include <vector>

using edge_tts::communication::VoiceFilter;
using edge_tts::communication::VoiceService;
using edge_tts::communication::FakeHttpClient;
using edge_tts::communication::default_edge_service_config;
using edge_tts::core::VoiceGender;
using edge_tts::serialization::VoiceJsonParser;
using edge_tts::common::ErrorCode;
using edge_tts::common::IdGenerator;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const VoiceJsonParser k_parser{};
static const auto k_cfg = default_edge_service_config();
static IdGenerator k_ids{};

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
    VoiceService svc{k_cfg, http, k_parser, k_ids};
    (void)svc.list_voices();
    EXPECT_EQ(http.last_request()->method, "GET");
}

TEST(VoiceService, SendsToVoicesEndpoint) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids};
    (void)svc.list_voices();
    EXPECT_EQ(http.last_request()->url, k_cfg.voices_endpoint);
}

TEST(VoiceService, UrlContainsTrustedClientToken) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids};
    (void)svc.list_voices();
    EXPECT_NE(http.last_request()->url.find(k_cfg.trusted_client_token), std::string::npos);
}

// ---------------------------------------------------------------------------
// Includes required headers
// ---------------------------------------------------------------------------

TEST(VoiceService, SetsUserAgentHeader) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids};
    (void)svc.list_voices();
    EXPECT_EQ(http.last_request()->headers.at("User-Agent"), k_cfg.user_agent);
}

TEST(VoiceService, SetsAcceptHeader) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids};
    (void)svc.list_voices();
    // Reference: VOICE_HEADERS["Accept"] = "*/*"
    EXPECT_EQ(http.last_request()->headers.at("Accept"), "*/*");
}

TEST(VoiceService, SetsAcceptLanguageHeader) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids};
    (void)svc.list_voices();
    EXPECT_EQ(http.last_request()->headers.at("Accept-Language"), "en-US,en;q=0.9");
}

TEST(VoiceService, SetsAcceptEncodingHeader) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids};
    (void)svc.list_voices();
    // Reference: BASE_HEADERS["Accept-Encoding"] = "gzip, deflate, br, zstd"
    EXPECT_EQ(http.last_request()->headers.at("Accept-Encoding"), "gzip, deflate, br, zstd");
}

TEST(VoiceService, SetsCookieMuidHeader) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids};
    (void)svc.list_voices();
    // Reference: DRM.headers_with_muid() → Cookie: muid=<32-upper-hex>;
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
    // Reference: drm.py DRM.generate_muid() called on every request.
    // Two consecutive list_voices() calls must produce different MUID cookies.
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    IdGenerator fresh_ids;
    VoiceService svc{k_cfg, http, k_parser, fresh_ids};

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
    VoiceService svc{k_cfg, http, k_parser, k_ids};

    const auto r = svc.list_voices();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 1u);
    EXPECT_EQ(r.value()[0].short_name, "en-US-EmmaMultilingualNeural");
    EXPECT_EQ(r.value()[0].locale,     "en-US");
}

TEST(VoiceService, EmptyArrayReturnsEmptyVector) {
    FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    VoiceService svc{k_cfg, http, k_parser, k_ids};
    const auto r = svc.list_voices();
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

// ---------------------------------------------------------------------------
// Non-2xx returns service error
// ---------------------------------------------------------------------------

TEST(VoiceService, Http403ReturnsServiceError) {
    FakeHttpClient http;
    http.set_response({403, {}, "Forbidden"});
    VoiceService svc{k_cfg, http, k_parser, k_ids};
    const auto r = svc.list_voices();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::service_error);
}

TEST(VoiceService, Http500ReturnsServiceError) {
    FakeHttpClient http;
    http.set_response({500, {}, "Internal Server Error"});
    VoiceService svc{k_cfg, http, k_parser, k_ids};
    const auto r = svc.list_voices();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::service_error);
}

TEST(VoiceService, Non200StatusCodeInErrorContext) {
    FakeHttpClient http;
    http.set_response({503, {}, ""});
    VoiceService svc{k_cfg, http, k_parser, k_ids};
    const auto r = svc.list_voices();
    EXPECT_FALSE(r.has_value());
    // Context should contain the status code
    EXPECT_NE(r.error().context().find("503"), std::string_view::npos);
}

// ---------------------------------------------------------------------------
// HTTP error propagates
// ---------------------------------------------------------------------------

TEST(VoiceService, HttpNetworkErrorPropagates) {
    FakeHttpClient http;
    http.set_error({ErrorCode::network_error, "connection refused"});
    VoiceService svc{k_cfg, http, k_parser, k_ids};
    const auto r = svc.list_voices();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::network_error);
}

TEST(VoiceService, HttpTimeoutPropagates) {
    FakeHttpClient http;
    http.set_error({ErrorCode::timeout, "read timeout"});
    VoiceService svc{k_cfg, http, k_parser, k_ids};
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
    VoiceService svc{k_cfg, http, k_parser, k_ids};
    const auto r = svc.list_voices();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

TEST(VoiceService, MissingRequiredFieldPropagatesParseError) {
    // Missing "Gender" field
    FakeHttpClient http;
    http.set_response({200, {},
        R"json([{"Name":"N","ShortName":"en-US-X","Locale":"en-US","SuggestedCodec":"mp3","FriendlyName":"F","Status":"GA"}])json"});
    VoiceService svc{k_cfg, http, k_parser, k_ids};
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
    VoiceService svc{k_cfg, http, k_parser, k_ids};

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
    VoiceService svc{k_cfg, http, k_parser, k_ids};

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
    VoiceService svc{k_cfg, http, k_parser, k_ids};

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
    VoiceService svc{k_cfg, http, k_parser, k_ids};

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
    VoiceService svc{k_cfg, http, k_parser, k_ids};

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
    VoiceService svc{k_cfg, http, k_parser, k_ids};

    VoiceFilter filter;
    filter.locale = "en-US";
    filter.gender = VoiceGender::female;
    const auto r = svc.list_voices(filter);

    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 1u);
    EXPECT_EQ(r.value()[0].short_name, "en-US-EmmaMultilingualNeural");
}

// ---------------------------------------------------------------------------
// Ordering matches reference
//
// Reference: list_voices() returns voices in wire order.
// Sorting (by ShortName) is done only by _print_voices() for CLI display.
// VoiceService::list_voices() must preserve wire order.
// ---------------------------------------------------------------------------

TEST(VoiceService, WireOrderPreserved) {
    FakeHttpClient http;
    http.set_response({200, {}, wire_order_json()});
    VoiceService svc{k_cfg, http, k_parser, k_ids};

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
    VoiceService svc{k_cfg, http, k_parser, k_ids};

    const auto r = svc.list_voices();
    EXPECT_TRUE(r.has_value());
    // If sorted alphabetically: de-DE-KatjaNeural, en-US-..., zh-CN-...
    // Wire order: zh-CN-..., en-US-..., de-DE-...  (NOT alphabetical)
    EXPECT_EQ(r.value()[0].short_name, "zh-CN-XiaoxiaoNeural"); // not "de-DE-KatjaNeural"
}
