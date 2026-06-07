#include "edge_tts/communication/EdgeRequestHeaders.hpp"
#include "edge_tts/communication/EdgeServiceConfig.hpp"
#include "edge_tts/communication/EdgeTokenProvider.hpp"
#include "edge_tts/common/Clock.hpp"
#include "edge_tts/common/IdGenerator.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>

using edge_tts::communication::build_websocket_headers;
using edge_tts::communication::build_voice_list_headers;
using edge_tts::communication::EdgeTokenProvider;
using edge_tts::communication::default_edge_service_config;
using edge_tts::common::FixedClock;
using edge_tts::common::IdGenerator;

static const auto k_cfg    = default_edge_service_config();
static FixedClock k_clock  {std::chrono::system_clock::from_time_t(1704067200)};
static EdgeTokenProvider k_tokens{k_cfg, k_clock};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Returns the value of the named header from a vector<pair>, or "" if absent.
static std::string ws_header(
    const std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& name)
{
    for (const auto& [k, v] : headers)
        if (k == name) return v;
    return {};
}

// Returns true iff the named header is present in a vector<pair>.
static bool ws_has(
    const std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& name)
{
    for (const auto& [k, v] : headers)
        if (k == name) return true;
    return false;
}

// ---------------------------------------------------------------------------
// build_websocket_headers — presence of all required headers
// ---------------------------------------------------------------------------

TEST(EdgeRequestHeaders, WebSocketHasPragma) {
    IdGenerator ids;
    const auto hdrs = build_websocket_headers(k_cfg, ids);
    EXPECT_EQ(ws_header(hdrs, "Pragma"), "no-cache");
}

TEST(EdgeRequestHeaders, WebSocketHasCacheControl) {
    IdGenerator ids;
    const auto hdrs = build_websocket_headers(k_cfg, ids);
    EXPECT_EQ(ws_header(hdrs, "Cache-Control"), "no-cache");
}

TEST(EdgeRequestHeaders, WebSocketHasOriginFromConfig) {
    IdGenerator ids;
    const auto hdrs = build_websocket_headers(k_cfg, ids);
    EXPECT_EQ(ws_header(hdrs, "Origin"), k_cfg.origin);
    EXPECT_FALSE(ws_header(hdrs, "Origin").empty());
}

TEST(EdgeRequestHeaders, WebSocketHasUserAgentFromConfig) {
    IdGenerator ids;
    const auto hdrs = build_websocket_headers(k_cfg, ids);
    EXPECT_EQ(ws_header(hdrs, "User-Agent"), k_cfg.user_agent);
    EXPECT_FALSE(ws_header(hdrs, "User-Agent").empty());
}

TEST(EdgeRequestHeaders, WebSocketHasAcceptEncoding) {
    IdGenerator ids;
    const auto hdrs = build_websocket_headers(k_cfg, ids);
    EXPECT_EQ(ws_header(hdrs, "Accept-Encoding"), "gzip, deflate, br, zstd");
}

TEST(EdgeRequestHeaders, WebSocketHasAcceptLanguage) {
    IdGenerator ids;
    const auto hdrs = build_websocket_headers(k_cfg, ids);
    EXPECT_EQ(ws_header(hdrs, "Accept-Language"), "en-US,en;q=0.9");
}

TEST(EdgeRequestHeaders, WebSocketHasCookieHeader) {
    IdGenerator ids;
    const auto hdrs = build_websocket_headers(k_cfg, ids);
    EXPECT_TRUE(ws_has(hdrs, "Cookie"));
    EXPECT_FALSE(ws_header(hdrs, "Cookie").empty());
}

// ---------------------------------------------------------------------------
// build_websocket_headers — MUID cookie format
// ---------------------------------------------------------------------------

TEST(EdgeRequestHeaders, WebSocketCookieStartsWithMuid) {
    IdGenerator ids;
    const auto hdrs = build_websocket_headers(k_cfg, ids);
    const std::string cookie = ws_header(hdrs, "Cookie");
    // Must start with "muid="
    EXPECT_EQ(cookie.substr(0, 5), "muid=");
}

TEST(EdgeRequestHeaders, WebSocketCookieHas32HexCharsAfterPrefix) {
    IdGenerator ids;
    const auto hdrs = build_websocket_headers(k_cfg, ids);
    const std::string cookie = ws_header(hdrs, "Cookie");
    // Format: "muid=<32 chars>;"
    // Length = 5 ("muid=") + 32 + 1 (";") = 38
    ASSERT_EQ(cookie.size(), 38u);
    const std::string hex_part = cookie.substr(5, 32);
    for (char c : hex_part)
        EXPECT_TRUE(std::isxdigit(static_cast<unsigned char>(c)));
}

TEST(EdgeRequestHeaders, WebSocketCookieMuidIsUppercase) {
    IdGenerator ids;
    const auto hdrs = build_websocket_headers(k_cfg, ids);
    const std::string cookie = ws_header(hdrs, "Cookie");
    const std::string hex_part = cookie.substr(5, 32);
    for (char c : hex_part) {
        // Must be digit or uppercase letter — no lowercase a-f.
        if (std::isalpha(static_cast<unsigned char>(c)))
            EXPECT_EQ(c, static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
}

TEST(EdgeRequestHeaders, WebSocketCookieEndsWithSemicolon) {
    IdGenerator ids;
    const auto hdrs = build_websocket_headers(k_cfg, ids);
    const std::string cookie = ws_header(hdrs, "Cookie");
    EXPECT_EQ(cookie.back(), ';');
}

TEST(EdgeRequestHeaders, WebSocketCookieExactFormat) {
    // Full format check: "muid=<32 UPPER HEX>;"
    IdGenerator ids;
    const auto hdrs = build_websocket_headers(k_cfg, ids);
    const std::string cookie = ws_header(hdrs, "Cookie");
    ASSERT_EQ(cookie.size(), 38u);
    EXPECT_EQ(cookie.substr(0, 5),  "muid=");
    EXPECT_EQ(cookie.substr(37, 1), ";");
    const std::string hex = cookie.substr(5, 32);
    for (char c : hex) {
        EXPECT_TRUE(std::isxdigit(static_cast<unsigned char>(c)));
        if (std::isalpha(static_cast<unsigned char>(c)))
            EXPECT_EQ(c, static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
}

// ---------------------------------------------------------------------------
// build_websocket_headers — fresh MUID per call
// ---------------------------------------------------------------------------

TEST(EdgeRequestHeaders, WebSocketEachCallProducesFreshMuid) {
    IdGenerator ids;
    const auto h1 = build_websocket_headers(k_cfg, ids);
    const auto h2 = build_websocket_headers(k_cfg, ids);
    const std::string c1 = ws_header(h1, "Cookie");
    const std::string c2 = ws_header(h2, "Cookie");
    // Two consecutive calls with the same generator must produce different MUIDs.
    EXPECT_NE(c1, c2);
}

// ---------------------------------------------------------------------------
// build_voice_list_headers — presence of all required headers
// ---------------------------------------------------------------------------

TEST(EdgeRequestHeaders, VoiceListHasUserAgentFromConfig) {
    IdGenerator ids;
    const auto hdrs = build_voice_list_headers(k_cfg, ids);
    EXPECT_EQ(hdrs.at("User-Agent"), k_cfg.user_agent);
    EXPECT_FALSE(hdrs.at("User-Agent").empty());
}

TEST(EdgeRequestHeaders, VoiceListHasAcceptEncoding) {
    IdGenerator ids;
    const auto hdrs = build_voice_list_headers(k_cfg, ids);
    EXPECT_EQ(hdrs.at("Accept-Encoding"), "gzip, deflate, br, zstd");
}

TEST(EdgeRequestHeaders, VoiceListHasAcceptLanguage) {
    IdGenerator ids;
    const auto hdrs = build_voice_list_headers(k_cfg, ids);
    EXPECT_EQ(hdrs.at("Accept-Language"), "en-US,en;q=0.9");
}

TEST(EdgeRequestHeaders, VoiceListHasAccept) {
    IdGenerator ids;
    const auto hdrs = build_voice_list_headers(k_cfg, ids);
    EXPECT_EQ(hdrs.at("Accept"), "*/*");
}

TEST(EdgeRequestHeaders, VoiceListHasCookieHeader) {
    IdGenerator ids;
    const auto hdrs = build_voice_list_headers(k_cfg, ids);
    EXPECT_NE(hdrs.find("Cookie"), hdrs.end());
    EXPECT_FALSE(hdrs.at("Cookie").empty());
}

// ---------------------------------------------------------------------------
// build_voice_list_headers — MUID cookie format
// ---------------------------------------------------------------------------

TEST(EdgeRequestHeaders, VoiceListCookieStartsWithMuid) {
    IdGenerator ids;
    const auto hdrs = build_voice_list_headers(k_cfg, ids);
    const std::string cookie = hdrs.at("Cookie");
    EXPECT_EQ(cookie.substr(0, 5), "muid=");
}

TEST(EdgeRequestHeaders, VoiceListCookieMuidIsUppercase) {
    IdGenerator ids;
    const auto hdrs = build_voice_list_headers(k_cfg, ids);
    const std::string cookie = hdrs.at("Cookie");
    const std::string hex_part = cookie.substr(5, 32);
    for (char c : hex_part) {
        if (std::isalpha(static_cast<unsigned char>(c)))
            EXPECT_EQ(c, static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
}

TEST(EdgeRequestHeaders, VoiceListCookieEndsWithSemicolon) {
    IdGenerator ids;
    const auto hdrs = build_voice_list_headers(k_cfg, ids);
    EXPECT_EQ(hdrs.at("Cookie").back(), ';');
}

TEST(EdgeRequestHeaders, VoiceListCookieExactFormat) {
    // Full format check: "muid=<32 UPPER HEX>;"
    IdGenerator ids;
    const auto hdrs = build_voice_list_headers(k_cfg, ids);
    const std::string cookie = hdrs.at("Cookie");
    ASSERT_EQ(cookie.size(), 38u);
    EXPECT_EQ(cookie.substr(0, 5),  "muid=");
    EXPECT_EQ(cookie.substr(37, 1), ";");
    const std::string hex = cookie.substr(5, 32);
    for (char c : hex) {
        EXPECT_TRUE(std::isxdigit(static_cast<unsigned char>(c)));
        if (std::isalpha(static_cast<unsigned char>(c)))
            EXPECT_EQ(c, static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
}

// ---------------------------------------------------------------------------
// build_voice_list_headers — fresh MUID per call
// ---------------------------------------------------------------------------

TEST(EdgeRequestHeaders, VoiceListEachCallProducesFreshMuid) {
    IdGenerator ids;
    const auto h1 = build_voice_list_headers(k_cfg, ids);
    const auto h2 = build_voice_list_headers(k_cfg, ids);
    EXPECT_NE(h1.at("Cookie"), h2.at("Cookie"));
}

// ---------------------------------------------------------------------------
// VoiceService end-to-end: build_voice_list_headers is actually used
// (verify Cookie/MUID reaches the HTTP client — no production code left
// manually building an incomplete header list)
// ---------------------------------------------------------------------------

#include "edge_tts/communication/FakeHttpClient.hpp"
#include "edge_tts/communication/VoiceService.hpp"
#include "edge_tts/serialization/VoiceJsonParser.hpp"

TEST(EdgeRequestHeaders, VoiceServiceSendsCookieHeader) {
    // VoiceService must send a Cookie header — verifies that no production code
    // still manually constructs an incomplete header list without Cookie/MUID.
    edge_tts::communication::FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    edge_tts::serialization::VoiceJsonParser parser;
    IdGenerator ids;

    edge_tts::communication::VoiceService svc{k_cfg, http, parser, ids, k_tokens};
    (void)svc.list_voices();

    ASSERT_TRUE(http.last_request().has_value());
    const auto& hdrs = http.last_request()->headers;
    EXPECT_NE(hdrs.find("Cookie"), hdrs.end());
    const std::string cookie = hdrs.at("Cookie");
    EXPECT_EQ(cookie.substr(0, 5), "muid=");
    EXPECT_EQ(cookie.back(), ';');
    // 32 uppercase hex chars after "muid="
    ASSERT_TRUE(cookie.size() >= 38u);
    const std::string hex = cookie.substr(5, 32);
    for (char c : hex) {
        EXPECT_TRUE(std::isxdigit(static_cast<unsigned char>(c)));
        if (std::isalpha(static_cast<unsigned char>(c)))
            EXPECT_EQ(c, static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
}

TEST(EdgeRequestHeaders, VoiceServiceMuidDiffersPerRequest) {
    edge_tts::communication::FakeHttpClient http;
    http.set_response({200, {}, "[]"});
    edge_tts::serialization::VoiceJsonParser parser;
    IdGenerator ids;

    edge_tts::communication::VoiceService svc{k_cfg, http, parser, ids, k_tokens};

    (void)svc.list_voices();
    const std::string cookie1 = http.last_request()->headers.at("Cookie");

    (void)svc.list_voices();
    const std::string cookie2 = http.last_request()->headers.at("Cookie");

    // Each voice-list request must use a fresh MUID.
    EXPECT_NE(cookie1, cookie2);
}
