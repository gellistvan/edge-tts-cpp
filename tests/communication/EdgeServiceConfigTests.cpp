#include "edge_tts/communication/EdgeServiceConfig.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <string>

using edge_tts::communication::EdgeServiceConfig;
using edge_tts::communication::default_edge_service_config;

// All expected values are derived verbatim from:
//   reference/edge-tts/src/edge_tts/constants.py
//   reference/edge-tts/src/edge_tts/communicate.py

static const EdgeServiceConfig cfg = default_edge_service_config();

// ---------------------------------------------------------------------------
// Endpoints: non-empty
// ---------------------------------------------------------------------------

TEST(EdgeServiceConfig, WebSocketEndpointNonEmpty) {
    EXPECT_FALSE(cfg.websocket_endpoint.empty());
}

TEST(EdgeServiceConfig, VoicesEndpointNonEmpty) {
    EXPECT_FALSE(cfg.voices_endpoint.empty());
}

// ---------------------------------------------------------------------------
// Endpoints: correct scheme
// ---------------------------------------------------------------------------

TEST(EdgeServiceConfig, WebSocketEndpointUsesWss) {
    EXPECT_EQ(cfg.websocket_endpoint.substr(0, 6), "wss://");
}

TEST(EdgeServiceConfig, VoicesEndpointUsesHttps) {
    EXPECT_EQ(cfg.voices_endpoint.substr(0, 8), "https://");
}

// ---------------------------------------------------------------------------
// Endpoints: contain trusted client token
// Reference: WSS_URL and VOICE_LIST both embed TRUSTED_CLIENT_TOKEN
// ---------------------------------------------------------------------------

TEST(EdgeServiceConfig, WebSocketEndpointContainsTrustedClientToken) {
    EXPECT_NE(cfg.websocket_endpoint.find(cfg.trusted_client_token), std::string::npos);
}

TEST(EdgeServiceConfig, VoicesEndpointContainsTrustedClientToken) {
    EXPECT_NE(cfg.voices_endpoint.find(cfg.trusted_client_token), std::string::npos);
}

// ---------------------------------------------------------------------------
// Endpoints: exact reference values
// Reference: constants.py WSS_URL / VOICE_LIST
// ---------------------------------------------------------------------------

TEST(EdgeServiceConfig, WebSocketEndpointExact) {
    EXPECT_EQ(cfg.websocket_endpoint,
        "wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud"
        "/edge/v1?TrustedClientToken=6A5AA1D4EAFF4E9FB37E23D68491D6F4");
}

TEST(EdgeServiceConfig, VoicesEndpointExact) {
    EXPECT_EQ(cfg.voices_endpoint,
        "https://speech.platform.bing.com/consumer/speech/synthesize/readaloud"
        "/voices/list?trustedclienttoken=6A5AA1D4EAFF4E9FB37E23D68491D6F4");
}

// Note: WSS_URL uses "TrustedClientToken" (mixed case) while VOICE_LIST uses
// "trustedclienttoken" (lower case) — this matches the reference exactly.
TEST(EdgeServiceConfig, WebSocketTokenParamIsMixedCase) {
    EXPECT_NE(cfg.websocket_endpoint.find("TrustedClientToken="), std::string::npos);
}

TEST(EdgeServiceConfig, VoicesTokenParamIsLowerCase) {
    EXPECT_NE(cfg.voices_endpoint.find("trustedclienttoken="), std::string::npos);
}

// ---------------------------------------------------------------------------
// Trusted client token
// Reference: constants.py TRUSTED_CLIENT_TOKEN = "6A5AA1D4EAFF4E9FB37E23D68491D6F4"
// ---------------------------------------------------------------------------

TEST(EdgeServiceConfig, TrustedClientTokenNonEmpty) {
    EXPECT_FALSE(cfg.trusted_client_token.empty());
}

TEST(EdgeServiceConfig, TrustedClientTokenExact) {
    EXPECT_EQ(cfg.trusted_client_token, "6A5AA1D4EAFF4E9FB37E23D68491D6F4");
}

// ---------------------------------------------------------------------------
// Sec-MS-GEC version
// Reference: constants.py SEC_MS_GEC_VERSION = f"1-{CHROMIUM_FULL_VERSION}"
//            CHROMIUM_FULL_VERSION = "143.0.3650.75"
// ---------------------------------------------------------------------------

TEST(EdgeServiceConfig, SecMsGecVersionNonEmpty) {
    EXPECT_FALSE(cfg.sec_ms_gec_version.empty());
}

TEST(EdgeServiceConfig, SecMsGecVersionStartsWith1Dash) {
    EXPECT_EQ(cfg.sec_ms_gec_version.substr(0, 2), "1-");
}

TEST(EdgeServiceConfig, SecMsGecVersionExact) {
    EXPECT_EQ(cfg.sec_ms_gec_version, "1-143.0.3650.75");
}

// ---------------------------------------------------------------------------
// Origin header
// Reference: constants.py WSS_HEADERS["Origin"]
// ---------------------------------------------------------------------------

TEST(EdgeServiceConfig, OriginNonEmpty) {
    EXPECT_FALSE(cfg.origin.empty());
}

TEST(EdgeServiceConfig, OriginExact) {
    EXPECT_EQ(cfg.origin, "chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold");
}

// ---------------------------------------------------------------------------
// User-Agent header
// Reference: constants.py BASE_HEADERS["User-Agent"]
//   f"Mozilla/5.0 ... Chrome/{CHROMIUM_MAJOR_VERSION}.0.0.0 Safari/537.36 Edg/{CHROMIUM_MAJOR_VERSION}.0.0.0"
//   with CHROMIUM_MAJOR_VERSION = "143"
// ---------------------------------------------------------------------------

TEST(EdgeServiceConfig, UserAgentNonEmpty) {
    EXPECT_FALSE(cfg.user_agent.empty());
}

TEST(EdgeServiceConfig, UserAgentStartsWithMozilla) {
    EXPECT_EQ(cfg.user_agent.substr(0, 8), "Mozilla/");
}

TEST(EdgeServiceConfig, UserAgentContainsEdg) {
    EXPECT_NE(cfg.user_agent.find("Edg/"), std::string::npos);
}

TEST(EdgeServiceConfig, UserAgentExact) {
    EXPECT_EQ(cfg.user_agent,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
        " (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36 Edg/143.0.0.0");
}

// ---------------------------------------------------------------------------
// Protocol frame paths
// Reference: communicate.py send_command_request(), ssml_headers_plus_data(), __stream()
// ---------------------------------------------------------------------------

TEST(EdgeServiceConfig, SpeechConfigPathNonEmpty) {
    EXPECT_FALSE(cfg.speech_config_path.empty());
}

TEST(EdgeServiceConfig, SpeechConfigPathExact) {
    EXPECT_EQ(cfg.speech_config_path, "speech.config");
}

TEST(EdgeServiceConfig, SsmlPathNonEmpty) {
    EXPECT_FALSE(cfg.ssml_path.empty());
}

TEST(EdgeServiceConfig, SsmlPathExact) {
    EXPECT_EQ(cfg.ssml_path, "ssml");
}

TEST(EdgeServiceConfig, AudioMetadataPathNonEmpty) {
    EXPECT_FALSE(cfg.audio_metadata_path.empty());
}

TEST(EdgeServiceConfig, AudioMetadataPathExact) {
    EXPECT_EQ(cfg.audio_metadata_path, "audio.metadata");
}

TEST(EdgeServiceConfig, TurnEndPathNonEmpty) {
    EXPECT_FALSE(cfg.turn_end_path.empty());
}

TEST(EdgeServiceConfig, TurnEndPathExact) {
    EXPECT_EQ(cfg.turn_end_path, "turn.end");
}

// ---------------------------------------------------------------------------
// All paths are distinct (no accidental collision between path constants)
// ---------------------------------------------------------------------------

TEST(EdgeServiceConfig, AllPathsDistinct) {
    EXPECT_NE(cfg.speech_config_path, cfg.ssml_path);
    EXPECT_NE(cfg.speech_config_path, cfg.audio_metadata_path);
    EXPECT_NE(cfg.speech_config_path, cfg.turn_end_path);
    EXPECT_NE(cfg.ssml_path,          cfg.audio_metadata_path);
    EXPECT_NE(cfg.ssml_path,          cfg.turn_end_path);
    EXPECT_NE(cfg.audio_metadata_path, cfg.turn_end_path);
}

// ---------------------------------------------------------------------------
// Endpoint host is speech.platform.bing.com
// ---------------------------------------------------------------------------

TEST(EdgeServiceConfig, EndpointsShareCommonHost) {
    const std::string host = "speech.platform.bing.com";
    EXPECT_NE(cfg.websocket_endpoint.find(host), std::string::npos);
    EXPECT_NE(cfg.voices_endpoint.find(host),    std::string::npos);
}

// ---------------------------------------------------------------------------
// Default config is deterministic (calling factory twice gives equal values)
// ---------------------------------------------------------------------------

TEST(EdgeServiceConfig, DefaultConfigIsDeterministic) {
    const EdgeServiceConfig a = default_edge_service_config();
    const EdgeServiceConfig b = default_edge_service_config();
    EXPECT_EQ(a.websocket_endpoint,    b.websocket_endpoint);
    EXPECT_EQ(a.voices_endpoint,       b.voices_endpoint);
    EXPECT_EQ(a.trusted_client_token,  b.trusted_client_token);
    EXPECT_EQ(a.sec_ms_gec_version,    b.sec_ms_gec_version);
    EXPECT_EQ(a.origin,                b.origin);
    EXPECT_EQ(a.user_agent,            b.user_agent);
    EXPECT_EQ(a.speech_config_path,    b.speech_config_path);
    EXPECT_EQ(a.ssml_path,             b.ssml_path);
    EXPECT_EQ(a.audio_metadata_path,   b.audio_metadata_path);
    EXPECT_EQ(a.turn_end_path,         b.turn_end_path);
}
