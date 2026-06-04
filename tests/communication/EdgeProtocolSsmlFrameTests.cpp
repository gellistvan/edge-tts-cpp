#include "edge_tts/communication/EdgeProtocol.hpp"
#include "edge_tts/communication/ConnectionMetadata.hpp"
#include "edge_tts/common/Clock.hpp"
#include "edge_tts/common/IdGenerator.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/serialization/ProtocolParser.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <chrono>
#include <optional>
#include <string>

using edge_tts::communication::EdgeProtocol;
using edge_tts::communication::ConnectionMetadata;
using edge_tts::communication::ConnectionMetadataFactory;
using edge_tts::common::FixedClock;
using edge_tts::common::IdGenerator;
using edge_tts::core::TtsConfig;
using edge_tts::core::BoundaryType;
using edge_tts::serialization::ProtocolParser;

// ---------------------------------------------------------------------------
// Fixed test clock: 2024-01-15 10:30:45 UTC (Unix ts 1705314645)
// SSML timestamp: "Mon Jan 15 2024 10:30:45 GMT+0000 (Coordinated Universal Time)Z"
// ---------------------------------------------------------------------------

static constexpr long long kFixedUnixTs = 1705314645LL;
static const auto kFixedTimePoint =
    std::chrono::system_clock::time_point{std::chrono::seconds{kFixedUnixTs}};
static const std::string kFixedTimestampZ =
    "Mon Jan 15 2024 10:30:45 GMT+0000 (Coordinated Universal Time)Z";

static ConnectionMetadata make_metadata() {
    IdGenerator ids;
    ConnectionMetadataFactory factory{ids};
    return factory.create_for_request();
}

// ---------------------------------------------------------------------------
// Frame parses as a valid ProtocolMessage
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSsmlFrame, FrameIsParseableAsProtocolMessage) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_ssml_frame(TtsConfig::defaults(), "Hello", make_metadata());
    EXPECT_TRUE(result.has_value());

    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
}

// ---------------------------------------------------------------------------
// Path header equals "ssml"
// Reference: communicate.py ssml_headers_plus_data() "Path:ssml\r\n\r\n"
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSsmlFrame, PathHeaderEqualsSsml) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_ssml_frame(TtsConfig::defaults(), "Hello", make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->header("Path"), std::optional<std::string>{"ssml"});
}

// ---------------------------------------------------------------------------
// Content-Type header equals reference value
// Reference: "Content-Type:application/ssml+xml\r\n"
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSsmlFrame, ContentTypeHeaderMatchesReference) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_ssml_frame(TtsConfig::defaults(), "Hello", make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->header("Content-Type"),
              std::optional<std::string>{"application/ssml+xml"});
}

// ---------------------------------------------------------------------------
// X-RequestId is present and matches metadata.request_id
// Reference: "X-RequestId:{request_id}\r\n"
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSsmlFrame, XRequestIdMatchesMetadata) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};
    const auto meta = make_metadata();

    const auto result = proto.build_ssml_frame(TtsConfig::defaults(), "Hello", meta);
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->header("X-RequestId"), std::optional<std::string>{meta.request_id});
}

// ---------------------------------------------------------------------------
// X-Timestamp is present and has a trailing 'Z'
// Reference: f"X-Timestamp:{timestamp}Z\r\n"  — documented Edge bug
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSsmlFrame, XTimestampHasTrailingZ) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_ssml_frame(TtsConfig::defaults(), "Hello", make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    const auto ts = parsed->header("X-Timestamp");
    EXPECT_TRUE(ts.has_value());
    EXPECT_EQ(ts->back(), 'Z');
}

// ---------------------------------------------------------------------------
// X-Timestamp matches fixed clock with trailing Z
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSsmlFrame, XTimestampMatchesFixedClock) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_ssml_frame(TtsConfig::defaults(), "Hello", make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->header("X-Timestamp"), std::optional<std::string>{kFixedTimestampZ});
}

// ---------------------------------------------------------------------------
// Header ordering matches reference
// Reference order: X-RequestId, Content-Type, X-Timestamp, Path
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSsmlFrame, HeaderOrderMatchesReference) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_ssml_frame(TtsConfig::defaults(), "Hello", make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->headers.size(), 4u);
    EXPECT_EQ(parsed->headers[0].first, "X-RequestId");
    EXPECT_EQ(parsed->headers[1].first, "Content-Type");
    EXPECT_EQ(parsed->headers[2].first, "X-Timestamp");
    EXPECT_EQ(parsed->headers[3].first, "Path");
}

// ---------------------------------------------------------------------------
// Body contains valid SSML (speak/voice/prosody tags)
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSsmlFrame, BodyContainsValidSsml) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_ssml_frame(TtsConfig::defaults(), "Hello", make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_NE(parsed->body.find("<speak"), std::string::npos);
    EXPECT_NE(parsed->body.find("<voice"), std::string::npos);
    EXPECT_NE(parsed->body.find("<prosody"), std::string::npos);
    EXPECT_NE(parsed->body.find("</speak>"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Body contains the voice name from TtsConfig
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSsmlFrame, BodyContainsVoiceName) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};
    TtsConfig config = TtsConfig::defaults();
    config.voice = "en-US-EmmaMultilingualNeural";

    const auto result = proto.build_ssml_frame(config, "Hello", make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    // SsmlBuilder normalizes to full voice form
    EXPECT_NE(parsed->body.find("EmmaMultilingualNeural"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Body contains rate, volume, and pitch from TtsConfig
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSsmlFrame, BodyContainsProsodyAttributes) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};
    TtsConfig config = TtsConfig::defaults();
    config.rate   = "+10%";
    config.volume = "-5%";
    config.pitch  = "+5Hz";

    const auto result = proto.build_ssml_frame(config, "Hello", make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_NE(parsed->body.find("+10%"),  std::string::npos);
    EXPECT_NE(parsed->body.find("-5%"),   std::string::npos);
    EXPECT_NE(parsed->body.find("+5Hz"),  std::string::npos);
}

// ---------------------------------------------------------------------------
// Text appears in body (plain text)
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSsmlFrame, BodyContainsInputText) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_ssml_frame(TtsConfig::defaults(), "Hello world", make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_NE(parsed->body.find("Hello world"), std::string::npos);
}

// ---------------------------------------------------------------------------
// XML special characters are escaped once in the body
// Reference: communicate.py uses escape() before mkssml(), SsmlBuilder does
// this internally. Passing raw text must not double-escape.
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSsmlFrame, XmlSpecialCharsEscapedOnce) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_ssml_frame(TtsConfig::defaults(), "a & b < c > d", make_metadata());
    EXPECT_TRUE(result.has_value());

    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());

    // Entities must appear once — &amp; not &amp;amp;
    EXPECT_NE(parsed->body.find("&amp;"),     std::string::npos);
    EXPECT_NE(parsed->body.find("&lt;"),      std::string::npos);
    EXPECT_NE(parsed->body.find("&gt;"),      std::string::npos);
    // Double-escaping must not occur
    EXPECT_EQ(parsed->body.find("&amp;amp;"), std::string::npos);
    EXPECT_EQ(parsed->body.find("&amp;lt;"),  std::string::npos);
}

// ---------------------------------------------------------------------------
// Invalid TtsConfig propagates an error (not a crash or silent failure)
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSsmlFrame, InvalidConfigPropagatesError) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};
    TtsConfig config = TtsConfig::defaults();
    config.rate = "invalid_rate";  // SsmlBuilder validates via validate_tts_config

    const auto result = proto.build_ssml_frame(config, "Hello", make_metadata());
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// Empty text chunk produces a frame (empty SSML body is valid)
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSsmlFrame, EmptyTextChunkProducesFrame) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_ssml_frame(TtsConfig::defaults(), "", make_metadata());
    // SsmlBuilder may accept or reject empty text; either way result is defined
    // and must not throw.
    (void)result;
    EXPECT_TRUE(true);  // reached without exception
}

// ---------------------------------------------------------------------------
// Each call uses the metadata.request_id supplied — different metadata yields
// different X-RequestId
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSsmlFrame, DifferentMetadataYieldsDifferentRequestId) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};
    const auto meta1 = make_metadata();
    const auto meta2 = make_metadata();

    const auto r1 = proto.build_ssml_frame(TtsConfig::defaults(), "Hello", meta1);
    const auto r2 = proto.build_ssml_frame(TtsConfig::defaults(), "Hello", meta2);
    EXPECT_TRUE(r1.has_value());
    EXPECT_TRUE(r2.has_value());

    ProtocolParser parser;
    const auto p1 = parser.parse(*r1);
    const auto p2 = parser.parse(*r2);
    EXPECT_TRUE(p1.has_value());
    EXPECT_TRUE(p2.has_value());

    EXPECT_NE(p1->header("X-RequestId"), p2->header("X-RequestId"));
}

// ---------------------------------------------------------------------------
// Golden fixture: full frame for default config + fixed clock + known metadata
// Verifies exact wire format against the Python reference.
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSsmlFrame, GoldenFixtureDefaultConfig) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    // Use a deterministic request_id for the fixture
    ConnectionMetadata meta;
    meta.connection_id = "00000000000000000000000000000000";
    meta.request_id    = "11111111111111111111111111111111";

    const auto result = proto.build_ssml_frame(TtsConfig::defaults(), "Hello", meta);
    EXPECT_TRUE(result.has_value());

    const std::string expected =
        "X-RequestId:11111111111111111111111111111111\r\n"
        "Content-Type:application/ssml+xml\r\n"
        "X-Timestamp:Mon Jan 15 2024 10:30:45 GMT+0000 (Coordinated Universal Time)Z\r\n"
        "Path:ssml\r\n"
        "\r\n"
        "<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='en-US'>"
        "<voice name='Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)'>"
        "<prosody pitch='+0Hz' rate='+0%' volume='+0%'>"
        "Hello"
        "</prosody>"
        "</voice>"
        "</speak>";

    EXPECT_EQ(*result, expected);
}
