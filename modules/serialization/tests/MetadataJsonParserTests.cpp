#include "serialization/MetadataJsonParser.hpp"
#include "core/Chunk.hpp"
#include "common/Error.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <string>
#include <vector>

using edge_tts::serialization::MetadataJsonParser;
using edge_tts::core::BoundaryChunk;
using edge_tts::core::BoundaryEventType;
using edge_tts::common::ErrorCode;

static MetadataJsonParser parser{};

// ---------------------------------------------------------------------------
// Word boundary
// ---------------------------------------------------------------------------

TEST(MetadataJsonParser, WordBoundary) {
    const std::string json = R"json({
      "Metadata": [
        {
          "Type": "WordBoundary",
          "Data": {
            "Offset": 6250000,
            "Duration": 5000000,
            "text": {
              "Text": "Hello",
              "Length": 5,
              "BoundaryType": "WordBoundary"
            }
          }
        }
      ]
    })json";

    const auto r = parser.parse(json);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 1u);

    const BoundaryChunk& c = r.value()[0];
    EXPECT_EQ(c.type,           BoundaryEventType::WordBoundary);
    EXPECT_EQ(c.offset_ticks,   6250000);
    EXPECT_EQ(c.duration_ticks, 5000000);
    EXPECT_EQ(c.text,           "Hello");
}

// ---------------------------------------------------------------------------
// Sentence boundary
// ---------------------------------------------------------------------------

TEST(MetadataJsonParser, SentenceBoundary) {
    const std::string json = R"json({
      "Metadata": [
        {
          "Type": "SentenceBoundary",
          "Data": {
            "Offset": 0,
            "Duration": 35000000,
            "text": {
              "Text": "Hello, world.",
              "Length": 13,
              "BoundaryType": "SentenceBoundary"
            }
          }
        }
      ]
    })json";

    const auto r = parser.parse(json);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 1u);
    EXPECT_EQ(r.value()[0].type,           BoundaryEventType::SentenceBoundary);
    EXPECT_EQ(r.value()[0].offset_ticks,   0);
    EXPECT_EQ(r.value()[0].duration_ticks, 35000000);
    EXPECT_EQ(r.value()[0].text,           "Hello, world.");
}

// ---------------------------------------------------------------------------
// Multiple boundaries in one metadata frame
// ---------------------------------------------------------------------------

TEST(MetadataJsonParser, MultipleBoundaries) {
    const std::string json = R"json({
      "Metadata": [
        {
          "Type": "WordBoundary",
          "Data": {
            "Offset": 1000000,
            "Duration": 2000000,
            "text": {"Text": "Hello", "Length": 5, "BoundaryType": "WordBoundary"}
          }
        },
        {
          "Type": "WordBoundary",
          "Data": {
            "Offset": 4000000,
            "Duration": 3000000,
            "text": {"Text": "world", "Length": 5, "BoundaryType": "WordBoundary"}
          }
        }
      ]
    })json";

    const auto r = parser.parse(json);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 2u);
    EXPECT_EQ(r.value()[0].text, "Hello");
    EXPECT_EQ(r.value()[0].offset_ticks, 1000000);
    EXPECT_EQ(r.value()[1].text, "world");
    EXPECT_EQ(r.value()[1].offset_ticks, 4000000);
}

// ---------------------------------------------------------------------------
// SessionEnd is silently skipped
// ---------------------------------------------------------------------------

TEST(MetadataJsonParser, SessionEndSkipped) {
    const std::string json = R"json({
      "Metadata": [
        {"Type": "SessionEnd", "Data": {}}
      ]
    })json";

    const auto r = parser.parse(json);
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

TEST(MetadataJsonParser, SessionEndSkippedMixedWithBoundary) {
    const std::string json = R"json({
      "Metadata": [
        {"Type": "SessionEnd", "Data": {}},
        {
          "Type": "WordBoundary",
          "Data": {
            "Offset": 100,
            "Duration": 200,
            "text": {"Text": "Hi", "Length": 2, "BoundaryType": "WordBoundary"}
          }
        }
      ]
    })json";

    const auto r = parser.parse(json);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 1u);
    EXPECT_EQ(r.value()[0].text, "Hi");
}

// ---------------------------------------------------------------------------
// Unknown metadata type → parse_error (reference raises UnknownResponse)
// ---------------------------------------------------------------------------

TEST(MetadataJsonParser, UnknownTypeRejected) {
    const std::string json = R"json({
      "Metadata": [
        {"Type": "SomeNewEvent", "Data": {}}
      ]
    })json";

    const auto r = parser.parse(json);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

// ---------------------------------------------------------------------------
// Missing required fields
// ---------------------------------------------------------------------------

TEST(MetadataJsonParser, MissingTextFieldRejected) {
    const std::string json = R"json({
      "Metadata": [
        {
          "Type": "WordBoundary",
          "Data": {
            "Offset": 100,
            "Duration": 200
          }
        }
      ]
    })json";

    const auto r = parser.parse(json);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

TEST(MetadataJsonParser, MissingOffsetRejected) {
    const std::string json = R"json({
      "Metadata": [
        {
          "Type": "WordBoundary",
          "Data": {
            "Duration": 200,
            "text": {"Text": "Hi", "Length": 2, "BoundaryType": "WordBoundary"}
          }
        }
      ]
    })json";

    const auto r = parser.parse(json);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

TEST(MetadataJsonParser, MissingDurationRejected) {
    const std::string json = R"json({
      "Metadata": [
        {
          "Type": "WordBoundary",
          "Data": {
            "Offset": 100,
            "text": {"Text": "Hi", "Length": 2, "BoundaryType": "WordBoundary"}
          }
        }
      ]
    })json";

    const auto r = parser.parse(json);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

TEST(MetadataJsonParser, MissingMetadataKeyRejected) {
    const auto r = parser.parse(R"json({"NotMetadata": []})json");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

// ---------------------------------------------------------------------------
// Invalid numeric fields
// ---------------------------------------------------------------------------

TEST(MetadataJsonParser, OffsetNotIntegerRejected) {
    const std::string json = R"json({
      "Metadata": [
        {
          "Type": "WordBoundary",
          "Data": {
            "Offset": "not-a-number",
            "Duration": 200,
            "text": {"Text": "Hi", "Length": 2, "BoundaryType": "WordBoundary"}
          }
        }
      ]
    })json";

    const auto r = parser.parse(json);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

TEST(MetadataJsonParser, DurationNotIntegerRejected) {
    const std::string json = R"json({
      "Metadata": [
        {
          "Type": "WordBoundary",
          "Data": {
            "Offset": 100,
            "Duration": 1.5,
            "text": {"Text": "Hi", "Length": 2, "BoundaryType": "WordBoundary"}
          }
        }
      ]
    })json";

    const auto r = parser.parse(json);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

// ---------------------------------------------------------------------------
// XML unescape applied to Text field
// ---------------------------------------------------------------------------

TEST(MetadataJsonParser, XmlUnescapeApplied) {
    // Reference: unescape(meta_obj["Data"]["text"]["Text"])
    // &amp; → &,  &lt; → <,  &gt; → >
    const std::string json = R"json({
      "Metadata": [
        {
          "Type": "WordBoundary",
          "Data": {
            "Offset": 0,
            "Duration": 100,
            "text": {
              "Text": "A &amp; B &lt;tag&gt;",
              "Length": 12,
              "BoundaryType": "WordBoundary"
            }
          }
        }
      ]
    })json";

    const auto r = parser.parse(json);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value()[0].text, "A & B <tag>");
}

// ---------------------------------------------------------------------------
// Malformed JSON
// ---------------------------------------------------------------------------

TEST(MetadataJsonParser, MalformedJsonRejected) {
    const auto r = parser.parse("not json {{{}}}");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

TEST(MetadataJsonParser, EmptyStringRejected) {
    const auto r = parser.parse("");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

// ---------------------------------------------------------------------------
// Empty Metadata array → empty vector
// ---------------------------------------------------------------------------

TEST(MetadataJsonParser, EmptyMetadataArrayReturnsEmptyVector) {
    const auto r = parser.parse(R"json({"Metadata": []})json");
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

// ---------------------------------------------------------------------------
// Root must be object with Metadata array
// ---------------------------------------------------------------------------

TEST(MetadataJsonParser, ArrayRootRejected) {
    const auto r = parser.parse("[]");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

TEST(MetadataJsonParser, MetadataNotArrayRejected) {
    const auto r = parser.parse(R"json({"Metadata": "not-an-array"})json");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

// ---------------------------------------------------------------------------
// Offset compensation is NOT applied (raw ticks returned)
// ---------------------------------------------------------------------------

TEST(MetadataJsonParser, OffsetCompensationNotApplied) {
    // The parser returns raw Offset value from JSON; compensation is the
    // communication layer's responsibility.
    const std::string json = R"json({
      "Metadata": [
        {
          "Type": "WordBoundary",
          "Data": {
            "Offset": 999999,
            "Duration": 1,
            "text": {"Text": "X", "Length": 1, "BoundaryType": "WordBoundary"}
          }
        }
      ]
    })json";

    const auto r = parser.parse(json);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value()[0].offset_ticks, 999999);
}
