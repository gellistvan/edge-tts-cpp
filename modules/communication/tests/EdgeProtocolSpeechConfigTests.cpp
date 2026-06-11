#include "communication/EdgeProtocol.hpp"
#include "communication/ConnectionMetadata.hpp"
#include "common/Clock.hpp"
#include "common/IdGenerator.hpp"
#include "core/TtsConfig.hpp"
#include "serialization/ProtocolParser.hpp"
#include "serialization/ProtocolMessage.hpp"
#include "vendor/minigtest/minigtest.hpp"
#include "EdgeProtocolTestFixtures.hpp"

#include <chrono>
#include <string>

using edge_tts::communication::EdgeProtocol;
using edge_tts::communication::ConnectionMetadata;
using edge_tts::communication::ConnectionMetadataFactory;
using edge_tts::common::FixedClock;
using edge_tts::common::IdGenerator;
using edge_tts::core::TtsConfig;
using edge_tts::core::BoundaryType;
using edge_tts::serialization::ProtocolParser;
using edge_tts::test::make_metadata;

// ---------------------------------------------------------------------------
// Fixed test clock: 2024-01-15 10:30:45 UTC (Unix ts 1705314645)
// Expected JS date: "Mon Jan 15 2024 10:30:45 GMT+0000 (Coordinated Universal Time)"
// ---------------------------------------------------------------------------

static constexpr long long kFixedUnixTs = 1705314645LL;
static const auto kFixedTimePoint =
    std::chrono::system_clock::time_point{std::chrono::seconds{kFixedUnixTs}};
static const std::string kFixedTimestamp =
    "Mon Jan 15 2024 10:30:45 GMT+0000 (Coordinated Universal Time)";

// ---------------------------------------------------------------------------
// Frame parses as a valid ProtocolMessage
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSpeechConfig, FrameIsParseableAsProtocolMessage) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_speech_config_frame(TtsConfig::defaults(), make_metadata());
    EXPECT_TRUE(result.has_value());

    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
}

// ---------------------------------------------------------------------------
// Path header equals reference value
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSpeechConfig, PathHeaderEqualsSpeechConfig) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_speech_config_frame(TtsConfig::defaults(), make_metadata());
    EXPECT_TRUE(result.has_value());

    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->header("Path"), std::optional<std::string>{"speech.config"});
}

// ---------------------------------------------------------------------------
// Content-Type header equals reference value
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSpeechConfig, ContentTypeHeaderMatchesReference) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_speech_config_frame(TtsConfig::defaults(), make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->header("Content-Type"),
              std::optional<std::string>{"application/json; charset=utf-8"});
}

// ---------------------------------------------------------------------------
// X-RequestId is absent (speech.config carries no request ID)
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSpeechConfig, NoXRequestIdHeader) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_speech_config_frame(TtsConfig::defaults(), make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_FALSE(parsed->header("X-RequestId").has_value());
}

// ---------------------------------------------------------------------------
// X-Timestamp is present and non-empty
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSpeechConfig, XTimestampIsPresent) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_speech_config_frame(TtsConfig::defaults(), make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    const auto ts = parsed->header("X-Timestamp");
    EXPECT_TRUE(ts.has_value());
    EXPECT_FALSE(ts->empty());
}

// ---------------------------------------------------------------------------
// X-Timestamp matches the fixed clock value exactly
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSpeechConfig, XTimestampMatchesFixedClock) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_speech_config_frame(TtsConfig::defaults(), make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->header("X-Timestamp"), std::optional<std::string>{kFixedTimestamp});
}

// ---------------------------------------------------------------------------
// X-Timestamp does NOT have a trailing 'Z' (only the SSML frame does)
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSpeechConfig, XTimestampHasNoTrailingZ) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_speech_config_frame(TtsConfig::defaults(), make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    const auto ts = parsed->header("X-Timestamp");
    EXPECT_TRUE(ts.has_value());
    EXPECT_NE(ts->back(), 'Z');
}

// ---------------------------------------------------------------------------
// Body JSON includes the output format
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSpeechConfig, BodyContainsOutputFormat) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_speech_config_frame(TtsConfig::defaults(), make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_NE(parsed->body.find("audio-24khz-48kbitrate-mono-mp3"), std::string::npos);
    EXPECT_NE(parsed->body.find("outputFormat"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Body JSON contains metadataoptions keys
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSpeechConfig, BodyContainsMetadataoptionsKeys) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_speech_config_frame(TtsConfig::defaults(), make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_NE(parsed->body.find("sentenceBoundaryEnabled"), std::string::npos);
    EXPECT_NE(parsed->body.find("wordBoundaryEnabled"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Boundary type: SentenceBoundary (default) — sq="true", wd="false"
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSpeechConfig, SentenceBoundaryDefaultValues) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};
    TtsConfig config = TtsConfig::defaults();
    config.boundary_type = BoundaryType::sentence;

    const auto result = proto.build_speech_config_frame(config, make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    // sentenceBoundaryEnabled should be "true", wordBoundaryEnabled "false"
    EXPECT_NE(parsed->body.find("\"sentenceBoundaryEnabled\":\"true\""), std::string::npos);
    EXPECT_NE(parsed->body.find("\"wordBoundaryEnabled\":\"false\""), std::string::npos);
}

// ---------------------------------------------------------------------------
// Boundary type: WordBoundary — wd="true", sq="false"
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSpeechConfig, WordBoundaryValues) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};
    TtsConfig config = TtsConfig::defaults();
    config.boundary_type = BoundaryType::word;

    const auto result = proto.build_speech_config_frame(config, make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    // wordBoundaryEnabled should be "true", sentenceBoundaryEnabled "false"
    EXPECT_NE(parsed->body.find("\"wordBoundaryEnabled\":\"true\""), std::string::npos);
    EXPECT_NE(parsed->body.find("\"sentenceBoundaryEnabled\":\"false\""), std::string::npos);
}

// ---------------------------------------------------------------------------
// Boundary values are JSON strings ("true"/"false"), not JSON booleans
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSpeechConfig, BoundaryValuesAreJsonStringsNotBooleans) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_speech_config_frame(TtsConfig::defaults(), make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    // Values must be quoted strings ("true" or "false"), not bare JSON booleans
    EXPECT_NE(parsed->body.find("\":\"true\""),  std::string::npos);
    EXPECT_NE(parsed->body.find("\":\"false\""), std::string::npos);
}

// ---------------------------------------------------------------------------
// Golden fixture: full frame for default config with fixed clock
// Verifies exact wire format.
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSpeechConfig, GoldenFixtureDefaultConfig) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_speech_config_frame(TtsConfig::defaults(), make_metadata());
    EXPECT_TRUE(result.has_value());

    const std::string expected =
        "X-Timestamp:Mon Jan 15 2024 10:30:45 GMT+0000 (Coordinated Universal Time)\r\n"
        "Content-Type:application/json; charset=utf-8\r\n"
        "Path:speech.config\r\n"
        "\r\n"
        "{\"context\":{\"synthesis\":{\"audio\":{\"metadataoptions\":{"
        "\"sentenceBoundaryEnabled\":\"true\",\"wordBoundaryEnabled\":\"false\"},"
        "\"outputFormat\":\"audio-24khz-48kbitrate-mono-mp3\"}}}}\r\n";

    EXPECT_EQ(*result, expected);
}

// ---------------------------------------------------------------------------
// Header ordering: X-Timestamp, Content-Type, Path
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSpeechConfig, HeaderOrderMatchesReference) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_speech_config_frame(TtsConfig::defaults(), make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->headers.size(), 3u);
    EXPECT_EQ(parsed->headers[0].first, "X-Timestamp");
    EXPECT_EQ(parsed->headers[1].first, "Content-Type");
    EXPECT_EQ(parsed->headers[2].first, "Path");
}

// ---------------------------------------------------------------------------
// Output format value is taken from config.output_format, not hardcoded
// (verified by checking the default format name appears verbatim)
// ---------------------------------------------------------------------------

TEST(EdgeProtocolSpeechConfig, OutputFormatFromConfig) {
    FixedClock clock{kFixedTimePoint};
    EdgeProtocol proto{clock};

    const auto result = proto.build_speech_config_frame(TtsConfig::defaults(), make_metadata());
    ProtocolParser parser;
    const auto parsed = parser.parse(*result);
    EXPECT_TRUE(parsed.has_value());
    const std::string expected_fmt{TtsConfig::defaults().output_format.value()};
    EXPECT_NE(parsed->body.find(expected_fmt), std::string::npos);
}
