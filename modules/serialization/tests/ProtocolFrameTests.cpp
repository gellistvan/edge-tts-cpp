#include "serialization/ProtocolMessage.hpp"
#include "serialization/ProtocolParser.hpp"
#include "serialization/ProtocolSerializer.hpp"
#include "common/Error.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <string>

using edge_tts::serialization::ProtocolMessage;
using edge_tts::serialization::ProtocolParser;
using edge_tts::serialization::ProtocolSerializer;
using edge_tts::common::ErrorCode;

static ProtocolParser    parser{};
static ProtocolSerializer serializer{};

// ---------------------------------------------------------------------------
// Parse basic frame
// ---------------------------------------------------------------------------

TEST(ProtocolParser, BasicFrame) {
    // Frame: X-RequestId:abc\r\nPath:ssml\r\n\r\n<speak>hi</speak>
    const std::string frame =
        "X-RequestId:abc\r\n"
        "Path:ssml\r\n"
        "\r\n"
        "<speak>hi</speak>";

    const auto r = parser.parse(frame);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().headers.size(), 2u);
    EXPECT_EQ(r.value().headers[0].first,  "X-RequestId");
    EXPECT_EQ(r.value().headers[0].second, "abc");
    EXPECT_EQ(r.value().headers[1].first,  "Path");
    EXPECT_EQ(r.value().headers[1].second, "ssml");
    EXPECT_EQ(r.value().body, "<speak>hi</speak>");
}

// ---------------------------------------------------------------------------
// Serialize basic frame
// ---------------------------------------------------------------------------

TEST(ProtocolSerializer, BasicFrame) {
    ProtocolMessage msg;
    msg.headers.emplace_back("X-RequestId", "abc");
    msg.headers.emplace_back("Path", "ssml");
    msg.body = "<speak>hi</speak>";

    const std::string out = serializer.serialize(msg);
    EXPECT_EQ(out,
        "X-RequestId:abc\r\n"
        "Path:ssml\r\n"
        "\r\n"
        "<speak>hi</speak>");
}

// ---------------------------------------------------------------------------
// Round trip: serialize then parse
// ---------------------------------------------------------------------------

TEST(ProtocolFrame, RoundTrip) {
    ProtocolMessage original;
    original.headers.emplace_back("X-Timestamp", "Mon Jan 01 2024 00:00:00 GMT+0000 (Coordinated Universal Time)Z");
    original.headers.emplace_back("Content-Type", "application/ssml+xml");
    original.headers.emplace_back("Path", "ssml");
    original.body = "<speak>Hello, world!</speak>";

    const std::string wire = serializer.serialize(original);
    const auto r = parser.parse(wire);
    EXPECT_TRUE(r.has_value());

    const auto& parsed = r.value();
    EXPECT_EQ(parsed.headers.size(), original.headers.size());
    for (std::size_t i = 0; i < original.headers.size(); ++i) {
        EXPECT_EQ(parsed.headers[i].first,  original.headers[i].first);
        EXPECT_EQ(parsed.headers[i].second, original.headers[i].second);
    }
    EXPECT_EQ(parsed.body, original.body);
}

// ---------------------------------------------------------------------------
// Empty body
// ---------------------------------------------------------------------------

TEST(ProtocolParser, EmptyBody) {
    const std::string frame = "Path:turn.end\r\n\r\n";
    const auto r = parser.parse(frame);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().headers.size(), 1u);
    EXPECT_EQ(r.value().headers[0].first,  "Path");
    EXPECT_EQ(r.value().headers[0].second, "turn.end");
    EXPECT_TRUE(r.value().body.empty());
}

TEST(ProtocolSerializer, EmptyBody) {
    ProtocolMessage msg;
    msg.headers.emplace_back("Path", "turn.end");
    const std::string out = serializer.serialize(msg);
    EXPECT_EQ(out, "Path:turn.end\r\n\r\n");
}

// ---------------------------------------------------------------------------
// Multiple headers
// ---------------------------------------------------------------------------

TEST(ProtocolParser, MultipleHeaders) {
    const std::string frame =
        "X-RequestId:req1\r\n"
        "Content-Type:application/json; charset=utf-8\r\n"
        "X-Timestamp:Mon Jan 01 2024 00:00:00 GMT+0000 (Coordinated Universal Time)\r\n"
        "Path:speech.config\r\n"
        "\r\n"
        "{\"context\":{}}";

    const auto r = parser.parse(frame);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().headers.size(), 4u);
    EXPECT_EQ(r.value().body, "{\"context\":{}}");
}

// ---------------------------------------------------------------------------
// Colon in value — split on first colon only
// ---------------------------------------------------------------------------

TEST(ProtocolParser, ColonInValue) {
    // "Content-Type:application/json; charset=utf-8" — colon appears in value too
    const std::string frame =
        "Content-Type:application/json; charset=utf-8\r\n"
        "\r\n";

    const auto r = parser.parse(frame);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().headers.size(), 1u);
    EXPECT_EQ(r.value().headers[0].first,  "Content-Type");
    EXPECT_EQ(r.value().headers[0].second, "application/json; charset=utf-8");
}

TEST(ProtocolParser, UrlInValue) {
    // Value contains multiple colons (URL)
    const std::string frame = "Location:https://example.com:443/path\r\n\r\n";
    const auto r = parser.parse(frame);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().headers[0].second, "https://example.com:443/path");
}

// ---------------------------------------------------------------------------
// Malformed header line — no colon
// ---------------------------------------------------------------------------

TEST(ProtocolParser, MalformedHeaderNoColon) {
    const std::string frame = "NoColonHere\r\n\r\n";
    const auto r = parser.parse(frame);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

// ---------------------------------------------------------------------------
// Missing separator (\r\n\r\n not present)
// ---------------------------------------------------------------------------

TEST(ProtocolParser, MissingSeparator) {
    const auto r = parser.parse("X-RequestId:abc\r\nPath:ssml\r\n");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

TEST(ProtocolParser, EmptyFrameRejected) {
    const auto r = parser.parse("");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

// ---------------------------------------------------------------------------
// CRLF vs LF behavior
// ---------------------------------------------------------------------------

TEST(ProtocolParser, LfOnlyNotAccepted) {
    // LF-only frame: \r\n\r\n separator cannot be found, returns parse_error
    const auto r = parser.parse("Path:ssml\n\n<body>");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

TEST(ProtocolParser, CrlfAccepted) {
    const auto r = parser.parse("Path:ssml\r\n\r\nbody");
    EXPECT_TRUE(r.has_value());
}

// ---------------------------------------------------------------------------
// Duplicate headers — both preserved in wire order
// ---------------------------------------------------------------------------

TEST(ProtocolParser, DuplicateHeadersPreservedInOrder) {
    const std::string frame =
        "X-RequestId:first\r\n"
        "X-RequestId:second\r\n"
        "\r\n";

    const auto r = parser.parse(frame);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().headers.size(), 2u);
    EXPECT_EQ(r.value().headers[0].second, "first");
    EXPECT_EQ(r.value().headers[1].second, "second");
    // header() lookup returns the first match
    const auto h = r.value().header("X-RequestId");
    EXPECT_TRUE(h.has_value());
    EXPECT_EQ(h.value(), "first");
}

// ---------------------------------------------------------------------------
// Header lookup helper
// ---------------------------------------------------------------------------

TEST(ProtocolMessage, HeaderLookupFound) {
    ProtocolMessage msg;
    msg.headers.emplace_back("Path", "audio.metadata");
    msg.headers.emplace_back("Content-Type", "application/json");
    const auto path_h = msg.header("Path");
    const auto ct_h   = msg.header("Content-Type");
    EXPECT_TRUE(path_h.has_value());
    EXPECT_TRUE(ct_h.has_value());
    EXPECT_EQ(path_h.value(), "audio.metadata");
    EXPECT_EQ(ct_h.value(), "application/json");
}

TEST(ProtocolMessage, HeaderLookupNotFound) {
    ProtocolMessage msg;
    msg.headers.emplace_back("Path", "ssml");
    EXPECT_FALSE(msg.header("X-RequestId").has_value());
}

// ---------------------------------------------------------------------------
// Edge cases for header values
// ---------------------------------------------------------------------------

TEST(ProtocolParser, EmptyHeaderValueAccepted) {
    // A header with nothing after the colon is valid; value is empty string.
    const std::string frame = "X-RequestId:\r\nPath:turn.end\r\n\r\n";
    const auto r = parser.parse(frame);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().headers.size(), 2u);
    EXPECT_EQ(r.value().headers[0].first,  "X-RequestId");
    EXPECT_EQ(r.value().headers[0].second, "");
}

TEST(ProtocolParser, EmptyHeaderSectionAccepted) {
    // Frame that starts immediately with the \r\n\r\n separator (no headers).
    const std::string frame = "\r\n\r\nbody";
    const auto r = parser.parse(frame);
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().headers.empty());
    EXPECT_EQ(r.value().body, "body");
}

TEST(ProtocolParser, HeaderValueNotTrimmed) {
    // The parser splits on the first colon only; it does not strip spaces.
    // Edge TTS wire frames do not add a space after ':', so "value" should
    // be returned verbatim (no leading-space stripping).
    const std::string frame = "Path:audio.metadata\r\n\r\n";
    const auto r = parser.parse(frame);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().headers[0].second, "audio.metadata");
}

TEST(ProtocolParser, MalformedHeaderLineContextContainsBadLine) {
    // The error context should include the raw offending header line so
    // callers can log it without re-parsing the frame.
    const std::string bad_line = "NoColonInThisLine";
    const std::string frame = bad_line + "\r\n\r\n";
    const auto r = parser.parse(frame);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
    // context() must contain the bad line verbatim
    const std::string ctx = std::string(r.error().context());
    EXPECT_NE(ctx.find(bad_line), std::string::npos);
}

// ---------------------------------------------------------------------------
// Regression fixture: basic protocol frame (protocol_basic.txt)
//
// Mirrors fixtures/protocol_basic.txt — the minimal well-formed frame shape.
// ---------------------------------------------------------------------------

TEST(ProtocolParser, BasicProtocolFrameRegression) {
    // Verbatim format used by the service for turn.start and similar control frames.
    const std::string frame =
        "X-RequestId:deadbeef00112233445566778899aabb\r\n"
        "Path:turn.start\r\n"
        "X-Timestamp:Mon Jan 01 2024 00:00:00 GMT+0000 (Coordinated Universal Time)Z\r\n"
        "\r\n";

    const auto r = parser.parse(frame);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().headers.size(), 3u);

    const auto path = r.value().header("Path");
    EXPECT_TRUE(path.has_value());
    EXPECT_EQ(path.value(), "turn.start");

    const auto req_id = r.value().header("X-RequestId");
    EXPECT_TRUE(req_id.has_value());
    EXPECT_EQ(req_id.value(), "deadbeef00112233445566778899aabb");

    EXPECT_TRUE(r.value().body.empty());
}

// ---------------------------------------------------------------------------
// Header order is preserved by serializer
// ---------------------------------------------------------------------------

TEST(ProtocolSerializer, HeaderOrderPreserved) {
    ProtocolMessage msg;
    msg.headers.emplace_back("A", "1");
    msg.headers.emplace_back("B", "2");
    msg.headers.emplace_back("C", "3");

    const std::string out = serializer.serialize(msg);
    EXPECT_EQ(out, "A:1\r\nB:2\r\nC:3\r\n\r\n");
}

// ---------------------------------------------------------------------------
// Serializer matches reference ssml_headers_plus_data format
// ---------------------------------------------------------------------------

TEST(ProtocolSerializer, SsmlFrameMatchesReference) {
    // Expected SSML frame header order:
    // f"X-RequestId:{request_id}\r\n"
    // "Content-Type:application/ssml+xml\r\n"
    // f"X-Timestamp:{timestamp}Z\r\n"
    // "Path:ssml\r\n\r\n"
    // f"{ssml}"
    ProtocolMessage msg;
    msg.headers.emplace_back("X-RequestId",  "deadbeef00112233445566778899aabb");
    msg.headers.emplace_back("Content-Type", "application/ssml+xml");
    msg.headers.emplace_back("X-Timestamp",  "Mon Jan 01 2024 00:00:00 GMT+0000 (Coordinated Universal Time)Z");
    msg.headers.emplace_back("Path",         "ssml");
    msg.body = "<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='en-US'>"
               "<voice name='Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)'>"
               "<prosody pitch='+0Hz' rate='+0%' volume='+0%'>Hello</prosody></voice></speak>";

    const std::string out = serializer.serialize(msg);
    const std::string expected =
        "X-RequestId:deadbeef00112233445566778899aabb\r\n"
        "Content-Type:application/ssml+xml\r\n"
        "X-Timestamp:Mon Jan 01 2024 00:00:00 GMT+0000 (Coordinated Universal Time)Z\r\n"
        "Path:ssml\r\n"
        "\r\n"
        "<speak version='1.0' xmlns='http://www.w3.org/2001/10/synthesis' xml:lang='en-US'>"
        "<voice name='Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)'>"
        "<prosody pitch='+0Hz' rate='+0%' volume='+0%'>Hello</prosody></voice></speak>";

    EXPECT_EQ(out, expected);
}
