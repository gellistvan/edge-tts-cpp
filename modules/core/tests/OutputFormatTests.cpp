#include "core/OutputFormat.hpp"
#include "vendor/minigtest/minigtest.hpp"

using edge_tts::core::OutputFormat;
using edge_tts::common::ErrorCode;

// ---------------------------------------------------------------------------
// default_format
// ---------------------------------------------------------------------------

TEST(OutputFormat, DefaultMatchesReference) {
    // Python communicate.py line 438: "outputFormat":"audio-24khz-48kbitrate-mono-mp3"
    // Store the object to keep the string_view alive during comparison.
    const auto fmt = OutputFormat::default_format();
    EXPECT_EQ(fmt.value(), "audio-24khz-48kbitrate-mono-mp3");
}

// ---------------------------------------------------------------------------
// from_string — valid input
// ---------------------------------------------------------------------------

TEST(OutputFormat, FromStringDefaultFormatSucceeds) {
    const auto result = OutputFormat::from_string("audio-24khz-48kbitrate-mono-mp3");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value().value(), "audio-24khz-48kbitrate-mono-mp3");
}

TEST(OutputFormat, FromStringPreservesValue) {
    const auto result = OutputFormat::from_string("audio-24khz-48kbitrate-mono-mp3");
    EXPECT_EQ(result.value().value(), "audio-24khz-48kbitrate-mono-mp3");
}

// ---------------------------------------------------------------------------
// from_string — invalid input
// ---------------------------------------------------------------------------

TEST(OutputFormat, EmptyStringFails) {
    const auto result = OutputFormat::from_string("");
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().code() == ErrorCode::invalid_argument);
}

TEST(OutputFormat, UnknownFormatFails) {
    const auto result = OutputFormat::from_string("audio-48khz-96kbitrate-mono-mp3");
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.error().code() == ErrorCode::unsupported);
}

TEST(OutputFormat, ArbitraryStringFails) {
    EXPECT_FALSE(OutputFormat::from_string("mp3").has_value());
    EXPECT_FALSE(OutputFormat::from_string("ogg").has_value());
    EXPECT_FALSE(OutputFormat::from_string("audio/mpeg").has_value());
    EXPECT_FALSE(OutputFormat::from_string("AUDIO-24KHZ-48KBITRATE-MONO-MP3").has_value());
}

TEST(OutputFormat, UnknownFormatCarriesContext) {
    const auto result = OutputFormat::from_string("not-a-format");
    EXPECT_TRUE(result.error().has_context());
    EXPECT_EQ(result.error().context(), "not-a-format");
}

// ---------------------------------------------------------------------------
// Equality
// ---------------------------------------------------------------------------

TEST(OutputFormat, TwoDefaultsAreEqual) {
    EXPECT_TRUE(OutputFormat::default_format() == OutputFormat::default_format());
    EXPECT_FALSE(OutputFormat::default_format() != OutputFormat::default_format());
}

TEST(OutputFormat, DefaultEqualsFromStringDefault) {
    const auto from_str = OutputFormat::from_string("audio-24khz-48kbitrate-mono-mp3");
    EXPECT_TRUE(from_str.has_value());
    EXPECT_TRUE(OutputFormat::default_format() == from_str.value());
}
