#include "edge_tts/serialization/VoiceJsonParser.hpp"
#include "edge_tts/core/Voice.hpp"
#include "edge_tts/common/Error.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <string>
#include <vector>

using edge_tts::serialization::VoiceJsonParser;
using edge_tts::core::Voice;
using edge_tts::core::VoiceGender;
using edge_tts::common::ErrorCode;

static VoiceJsonParser parser{};

// ---------------------------------------------------------------------------
// Valid list
// ---------------------------------------------------------------------------

TEST(VoiceJsonParser, ValidSingleVoice) {
    const std::string json = R"json([
      {
        "Name": "Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)",
        "ShortName": "en-US-EmmaMultilingualNeural",
        "Gender": "Female",
        "Locale": "en-US",
        "SuggestedCodec": "audio-24khz-48kbitrate-mono-mp3",
        "FriendlyName": "Microsoft Emma Online (Natural) - English (United States)",
        "Status": "GA",
        "VoiceTag": {
          "ContentCategories": ["General"],
          "VoicePersonalities": ["Friendly", "Positive"]
        }
      }
    ])json";

    const auto r = parser.parse(json);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 1u);

    const Voice& v = r.value()[0];
    EXPECT_EQ(v.name, "Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)");
    EXPECT_EQ(v.short_name, "en-US-EmmaMultilingualNeural");
    EXPECT_EQ(v.gender, VoiceGender::female);
    EXPECT_EQ(v.locale, "en-US");
    EXPECT_EQ(v.suggested_codec, "audio-24khz-48kbitrate-mono-mp3");
    EXPECT_EQ(v.friendly_name, "Microsoft Emma Online (Natural) - English (United States)");
    EXPECT_EQ(v.status, "GA");
    EXPECT_EQ(v.content_categories.size(), 1u);
    EXPECT_EQ(v.content_categories[0], "General");
    EXPECT_EQ(v.voice_personalities.size(), 2u);
    EXPECT_EQ(v.voice_personalities[0], "Friendly");
    EXPECT_EQ(v.voice_personalities[1], "Positive");
}

// ---------------------------------------------------------------------------
// Multiple voices
// ---------------------------------------------------------------------------

TEST(VoiceJsonParser, MultipleVoices) {
    const std::string json = R"json([
      {
        "Name": "Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)",
        "ShortName": "en-US-EmmaMultilingualNeural",
        "Gender": "Female",
        "Locale": "en-US",
        "SuggestedCodec": "audio-24khz-48kbitrate-mono-mp3",
        "FriendlyName": "Microsoft Emma Online (Natural) - English (United States)",
        "Status": "GA",
        "VoiceTag": {"ContentCategories": [], "VoicePersonalities": []}
      },
      {
        "Name": "Microsoft Server Speech Text to Speech Voice (en-GB, RyanNeural)",
        "ShortName": "en-GB-RyanNeural",
        "Gender": "Male",
        "Locale": "en-GB",
        "SuggestedCodec": "audio-24khz-48kbitrate-mono-mp3",
        "FriendlyName": "Microsoft Ryan Online (Natural) - English (United Kingdom)",
        "Status": "GA",
        "VoiceTag": {"ContentCategories": ["General"], "VoicePersonalities": ["Reliable"]}
      }
    ])json";

    const auto r = parser.parse(json);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 2u);
    EXPECT_EQ(r.value()[0].short_name, "en-US-EmmaMultilingualNeural");
    EXPECT_EQ(r.value()[1].short_name, "en-GB-RyanNeural");
}

// ---------------------------------------------------------------------------
// Unknown fields are ignored
// ---------------------------------------------------------------------------

TEST(VoiceJsonParser, UnknownFieldsIgnored) {
    const std::string json = R"json([
      {
        "Name": "Microsoft Server Speech Text to Speech Voice (en-US, GuyNeural)",
        "ShortName": "en-US-GuyNeural",
        "Gender": "Male",
        "Locale": "en-US",
        "SuggestedCodec": "audio-24khz-48kbitrate-mono-mp3",
        "FriendlyName": "Microsoft Guy Online (Natural) - English (United States)",
        "Status": "GA",
        "VoiceTag": {"ContentCategories": [], "VoicePersonalities": []},
        "UnknownField": "should be ignored",
        "AnotherUnknown": 42
      }
    ])json";

    const auto r = parser.parse(json);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 1u);
    EXPECT_EQ(r.value()[0].short_name, "en-US-GuyNeural");
}

// ---------------------------------------------------------------------------
// Missing optional VoiceTag — defaults to empty lists
// ---------------------------------------------------------------------------

TEST(VoiceJsonParser, MissingVoiceTagDefaultsToEmpty) {
    const std::string json = R"json([
      {
        "Name": "Microsoft Server Speech Text to Speech Voice (en-GB, RyanNeural)",
        "ShortName": "en-GB-RyanNeural",
        "Gender": "Male",
        "Locale": "en-GB",
        "SuggestedCodec": "audio-24khz-48kbitrate-mono-mp3",
        "FriendlyName": "Microsoft Ryan Online (Natural) - English (United Kingdom)",
        "Status": "GA"
      }
    ])json";

    const auto r = parser.parse(json);
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value()[0].content_categories.empty());
    EXPECT_TRUE(r.value()[0].voice_personalities.empty());
}

// ---------------------------------------------------------------------------
// Missing ContentCategories or VoicePersonalities inside VoiceTag
// ---------------------------------------------------------------------------

TEST(VoiceJsonParser, MissingVoiceTagSubListsDefaultToEmpty) {
    const std::string json = R"json([
      {
        "Name": "Microsoft Server Speech Text to Speech Voice (en-GB, RyanNeural)",
        "ShortName": "en-GB-RyanNeural",
        "Gender": "Male",
        "Locale": "en-GB",
        "SuggestedCodec": "audio-24khz-48kbitrate-mono-mp3",
        "FriendlyName": "Microsoft Ryan Online (Natural) - English (United Kingdom)",
        "Status": "GA",
        "VoiceTag": {}
      }
    ])json";

    const auto r = parser.parse(json);
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value()[0].content_categories.empty());
    EXPECT_TRUE(r.value()[0].voice_personalities.empty());
}

// ---------------------------------------------------------------------------
// Missing required fields → rejected
// ---------------------------------------------------------------------------

TEST(VoiceJsonParser, MissingNameRejected) {
    const std::string json = R"json([
      {
        "ShortName": "en-US-EmmaMultilingualNeural",
        "Gender": "Female",
        "Locale": "en-US",
        "SuggestedCodec": "audio-24khz-48kbitrate-mono-mp3",
        "FriendlyName": "Microsoft Emma",
        "Status": "GA"
      }
    ])json";
    const auto r = parser.parse(json);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

TEST(VoiceJsonParser, MissingGenderRejected) {
    const std::string json = R"json([
      {
        "Name": "Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)",
        "ShortName": "en-US-EmmaMultilingualNeural",
        "Locale": "en-US",
        "SuggestedCodec": "audio-24khz-48kbitrate-mono-mp3",
        "FriendlyName": "Microsoft Emma",
        "Status": "GA"
      }
    ])json";
    const auto r = parser.parse(json);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

TEST(VoiceJsonParser, MissingLocaleRejected) {
    const std::string json = R"json([
      {
        "Name": "Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)",
        "ShortName": "en-US-EmmaMultilingualNeural",
        "Gender": "Female",
        "SuggestedCodec": "audio-24khz-48kbitrate-mono-mp3",
        "FriendlyName": "Microsoft Emma",
        "Status": "GA"
      }
    ])json";
    const auto r = parser.parse(json);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

// ---------------------------------------------------------------------------
// Malformed JSON
// ---------------------------------------------------------------------------

TEST(VoiceJsonParser, MalformedJsonRejected) {
    const auto r = parser.parse("this is not valid json {{{");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

TEST(VoiceJsonParser, EmptyStringRejected) {
    const auto r = parser.parse("");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

// ---------------------------------------------------------------------------
// Invalid root type (object instead of array)
// ---------------------------------------------------------------------------

TEST(VoiceJsonParser, ObjectRootRejected) {
    const auto r = parser.parse(R"json({"Name": "foo"})json");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

TEST(VoiceJsonParser, NullRootRejected) {
    const auto r = parser.parse("null");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

TEST(VoiceJsonParser, StringRootRejected) {
    const auto r = parser.parse(R"json("hello")json");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

// ---------------------------------------------------------------------------
// Empty array
// ---------------------------------------------------------------------------

TEST(VoiceJsonParser, EmptyArrayReturnsEmptyVector) {
    const auto r = parser.parse("[]");
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

// ---------------------------------------------------------------------------
// Gender conversion
// ---------------------------------------------------------------------------

TEST(VoiceJsonParser, GenderFemaleConverted) {
    const std::string json = R"json([
      {
        "Name": "N",
        "ShortName": "en-US-Test",
        "Gender": "Female",
        "Locale": "en-US",
        "SuggestedCodec": "mp3",
        "FriendlyName": "Test",
        "Status": "GA"
      }
    ])json";
    const auto r = parser.parse(json);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value()[0].gender, VoiceGender::female);
}

TEST(VoiceJsonParser, GenderMaleConverted) {
    const std::string json = R"json([
      {
        "Name": "N",
        "ShortName": "en-US-Test",
        "Gender": "Male",
        "Locale": "en-US",
        "SuggestedCodec": "mp3",
        "FriendlyName": "Test",
        "Status": "GA"
      }
    ])json";
    const auto r = parser.parse(json);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value()[0].gender, VoiceGender::male);
}

TEST(VoiceJsonParser, UnknownGenderRejected) {
    const std::string json = R"json([
      {
        "Name": "N",
        "ShortName": "en-US-Test",
        "Gender": "Nonbinary",
        "Locale": "en-US",
        "SuggestedCodec": "mp3",
        "FriendlyName": "Test",
        "Status": "GA"
      }
    ])json";
    const auto r = parser.parse(json);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

// ---------------------------------------------------------------------------
// Locale preservation and language derivation
// ---------------------------------------------------------------------------

TEST(VoiceJsonParser, LocalePreservedExactly) {
    const std::string json = R"json([
      {
        "Name": "N",
        "ShortName": "zh-CN-Test",
        "Gender": "Female",
        "Locale": "zh-CN",
        "SuggestedCodec": "mp3",
        "FriendlyName": "Test",
        "Status": "GA"
      }
    ])json";
    const auto r = parser.parse(json);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value()[0].locale, "zh-CN");
    EXPECT_EQ(r.value()[0].language, "zh");
}

TEST(VoiceJsonParser, LocaleWithMultipleDashesLanguageIsFirstSegment) {
    const std::string json = R"json([
      {
        "Name": "N",
        "ShortName": "fil-PH-Test",
        "Gender": "Female",
        "Locale": "fil-PH",
        "SuggestedCodec": "mp3",
        "FriendlyName": "Test",
        "Status": "GA"
      }
    ])json";
    const auto r = parser.parse(json);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value()[0].locale, "fil-PH");
    EXPECT_EQ(r.value()[0].language, "fil");
}

// ---------------------------------------------------------------------------
// Ordering preserved (reference list_voices returns array in wire order)
// ---------------------------------------------------------------------------

TEST(VoiceJsonParser, OrderingPreservedFromWire) {
    const std::string json = R"json([
      {
        "Name": "N1", "ShortName": "zh-CN-Zzz", "Gender": "Female",
        "Locale": "zh-CN", "SuggestedCodec": "mp3", "FriendlyName": "F1", "Status": "GA"
      },
      {
        "Name": "N2", "ShortName": "en-US-Aaa", "Gender": "Male",
        "Locale": "en-US", "SuggestedCodec": "mp3", "FriendlyName": "F2", "Status": "GA"
      },
      {
        "Name": "N3", "ShortName": "de-DE-Mmm", "Gender": "Female",
        "Locale": "de-DE", "SuggestedCodec": "mp3", "FriendlyName": "F3", "Status": "Preview"
      }
    ])json";
    const auto r = parser.parse(json);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().size(), 3u);
    EXPECT_EQ(r.value()[0].short_name, "zh-CN-Zzz");
    EXPECT_EQ(r.value()[1].short_name, "en-US-Aaa");
    EXPECT_EQ(r.value()[2].short_name, "de-DE-Mmm");
}
