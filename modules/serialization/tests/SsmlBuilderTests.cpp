#include "serialization/SsmlBuilder.hpp"
#include "core/TtsConfig.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <string>

using edge_tts::serialization::SsmlBuilder;
using edge_tts::core::TtsConfig;
using edge_tts::common::ErrorCode;

static SsmlBuilder builder{};

// Expected SSML for TtsConfig::defaults() + "Hello, world!".
// Mirrors tests/serialization/fixtures/ssml_default.xml exactly.
// Single-quoted attributes, prosody order: pitch, rate, volume.
// xml:lang is hardcoded 'en-US' per the Python reference.
static const std::string k_default_ssml =
    "<speak version='1.0'"
    " xmlns='http://www.w3.org/2001/10/synthesis'"
    " xml:lang='en-US'>"
    "<voice name='Microsoft Server Speech Text to Speech Voice"
             " (en-US, EmmaMultilingualNeural)'>"
    "<prosody pitch='+0Hz' rate='+0%' volume='+0%'>"
    "Hello, world!"
    "</prosody>"
    "</voice>"
    "</speak>";

// ---------------------------------------------------------------------------
// Fixture stability — compare against the documented ground truth
// ---------------------------------------------------------------------------

TEST(SsmlBuilder, DefaultConfigMatchesFixture) {
    const auto r = builder.build(TtsConfig::defaults(), "Hello, world!");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), k_default_ssml);
}

// ---------------------------------------------------------------------------
// Voice name placement
// ---------------------------------------------------------------------------

TEST(SsmlBuilder, ShortVoiceNameNormalizedToFullForm) {
    // Short form "en-US-GuyNeural" must appear in the SSML as the full form.
    TtsConfig cfg = TtsConfig::defaults();
    cfg.voice = "en-US-GuyNeural";
    const auto r = builder.build(cfg, "test");
    EXPECT_TRUE(r.has_value());
    const auto& ssml = r.value();
    EXPECT_NE(ssml.find(
        "Microsoft Server Speech Text to Speech Voice (en-US, GuyNeural)"),
        std::string::npos);
    // Short form must NOT appear verbatim.
    EXPECT_EQ(ssml.find("en-US-GuyNeural"), std::string::npos);
}

TEST(SsmlBuilder, FullVoiceNamePassedThrough) {
    TtsConfig cfg = TtsConfig::defaults();
    cfg.voice =
        "Microsoft Server Speech Text to Speech Voice (en-US, GuyNeural)";
    const auto r = builder.build(cfg, "test");
    EXPECT_TRUE(r.has_value());
    EXPECT_NE(r.value().find(
        "Microsoft Server Speech Text to Speech Voice (en-US, GuyNeural)"),
        std::string::npos);
}

// ---------------------------------------------------------------------------
// Prosody attributes — each must appear with its value
// ---------------------------------------------------------------------------

TEST(SsmlBuilder, RateAppearsInProsody) {
    TtsConfig cfg = TtsConfig::defaults();
    cfg.rate = "+50%";
    const auto r = builder.build(cfg, "x");
    EXPECT_TRUE(r.has_value());
    EXPECT_NE(r.value().find("rate='+50%'"), std::string::npos);
}

TEST(SsmlBuilder, VolumeAppearsInProsody) {
    TtsConfig cfg = TtsConfig::defaults();
    cfg.volume = "-20%";
    const auto r = builder.build(cfg, "x");
    EXPECT_TRUE(r.has_value());
    EXPECT_NE(r.value().find("volume='-20%'"), std::string::npos);
}

TEST(SsmlBuilder, PitchAppearsInProsody) {
    TtsConfig cfg = TtsConfig::defaults();
    cfg.pitch = "+100Hz";
    const auto r = builder.build(cfg, "x");
    EXPECT_TRUE(r.has_value());
    EXPECT_NE(r.value().find("pitch='+100Hz'"), std::string::npos);
}

TEST(SsmlBuilder, ProsodyAttributeOrderMatchesReference) {
    // Reference: pitch, rate, volume (in that order).
    TtsConfig cfg = TtsConfig::defaults();
    cfg.pitch  = "+10Hz";
    cfg.rate   = "+5%";
    cfg.volume = "-5%";
    const auto r = builder.build(cfg, "x");
    EXPECT_TRUE(r.has_value());
    const auto& s = r.value();
    const auto pitch_pos  = s.find("pitch=");
    const auto rate_pos   = s.find("rate=");
    const auto volume_pos = s.find("volume=");
    EXPECT_NE(pitch_pos,  std::string::npos);
    EXPECT_NE(rate_pos,   std::string::npos);
    EXPECT_NE(volume_pos, std::string::npos);
    EXPECT_TRUE(pitch_pos < rate_pos);
    EXPECT_TRUE(rate_pos  < volume_pos);
}

// ---------------------------------------------------------------------------
// xml:lang is always 'en-US' regardless of voice locale
// ---------------------------------------------------------------------------

TEST(SsmlBuilder, XmlLangIsAlwaysEnUS) {
    TtsConfig cfg = TtsConfig::defaults();
    cfg.voice = "fr-FR-HenriNeural";
    const auto r = builder.build(cfg, "bonjour");
    EXPECT_TRUE(r.has_value());
    EXPECT_NE(r.value().find("xml:lang='en-US'"), std::string::npos);
}

// ---------------------------------------------------------------------------
// XML escaping of raw text content
// ---------------------------------------------------------------------------

TEST(SsmlBuilder, AmpersandEscaped) {
    const auto r = builder.build(TtsConfig::defaults(), "a & b");
    EXPECT_TRUE(r.has_value());
    EXPECT_NE(r.value().find("a &amp; b"), std::string::npos);
    EXPECT_EQ(r.value().find("a & b"),     std::string::npos);
}

TEST(SsmlBuilder, LessThanEscaped) {
    const auto r = builder.build(TtsConfig::defaults(), "1 < 2");
    EXPECT_TRUE(r.has_value());
    EXPECT_NE(r.value().find("1 &lt; 2"), std::string::npos);
}

TEST(SsmlBuilder, GreaterThanEscaped) {
    const auto r = builder.build(TtsConfig::defaults(), "2 > 1");
    EXPECT_TRUE(r.has_value());
    EXPECT_NE(r.value().find("2 &gt; 1"), std::string::npos);
}

// ---------------------------------------------------------------------------
// No double-escaping: raw_text is escaped exactly once
// ---------------------------------------------------------------------------

TEST(SsmlBuilder, NoDoubleEscaping) {
    // raw_text = "a&b" — should appear as "a&amp;b" in SSML, not "a&amp;amp;b".
    const auto r = builder.build(TtsConfig::defaults(), "a&b");
    EXPECT_TRUE(r.has_value());
    EXPECT_NE(r.value().find("a&amp;b"),     std::string::npos);
    EXPECT_EQ(r.value().find("a&amp;amp;b"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Unicode preservation
// ---------------------------------------------------------------------------

TEST(SsmlBuilder, UnicodePreservedInContent) {
    // Japanese text — multi-byte UTF-8 must pass through unchanged.
    const std::string jp = "\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF";
    const auto r = builder.build(TtsConfig::defaults(), jp);
    EXPECT_TRUE(r.has_value());
    EXPECT_NE(r.value().find(jp), std::string::npos);
}

TEST(SsmlBuilder, EmojiPreservedInContent) {
    const std::string emoji = "\xF0\x9F\x98\x80";  // U+1F600 😀
    const auto r = builder.build(TtsConfig::defaults(), emoji);
    EXPECT_TRUE(r.has_value());
    EXPECT_NE(r.value().find(emoji), std::string::npos);
}

// ---------------------------------------------------------------------------
// Empty text — allowed (service just produces silence)
// ---------------------------------------------------------------------------

TEST(SsmlBuilder, EmptyTextProducesValidSsml) {
    const auto r = builder.build(TtsConfig::defaults(), "");
    EXPECT_TRUE(r.has_value());
    // Must contain the full structural shell.
    EXPECT_NE(r.value().find("<speak"), std::string::npos);
    EXPECT_NE(r.value().find("<prosody"), std::string::npos);
    EXPECT_NE(r.value().find("</speak>"), std::string::npos);
    // Prosody close follows immediately after open (no content).
    EXPECT_NE(r.value().find("'>"),      std::string::npos);
}

// ---------------------------------------------------------------------------
// Control characters replaced before escaping
// ---------------------------------------------------------------------------

TEST(SsmlBuilder, ControlCharsReplacedInContent) {
    // VT (\x0B) is replaced by space by TextNormalizer.
    const auto r = builder.build(TtsConfig::defaults(), "hello\x0Bworld");
    EXPECT_TRUE(r.has_value());
    EXPECT_NE(r.value().find("hello world"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Invalid config → error propagated
// ---------------------------------------------------------------------------

TEST(SsmlBuilder, InvalidVoiceReturnsError) {
    TtsConfig bad = TtsConfig::defaults();
    bad.voice = "not-a-valid-voice";
    const auto r = builder.build(bad, "hello");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_argument);
}

TEST(SsmlBuilder, InvalidRateReturnsError) {
    TtsConfig bad = TtsConfig::defaults();
    bad.rate = "fast";
    const auto r = builder.build(bad, "hello");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_argument);
}

// ---------------------------------------------------------------------------
// Invalid UTF-8 input → error propagated from TextNormalizer
// ---------------------------------------------------------------------------

TEST(SsmlBuilder, InvalidUtf8ReturnsError) {
    const auto r = builder.build(TtsConfig::defaults(), "\x80");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_argument);
}

// ---------------------------------------------------------------------------
// No protocol framing in output
// ---------------------------------------------------------------------------

TEST(SsmlBuilder, OutputContainsNoProtocolHeaders) {
    const auto r = builder.build(TtsConfig::defaults(), "hello");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().find("X-RequestId"),  std::string::npos);
    EXPECT_EQ(r.value().find("X-Timestamp"),  std::string::npos);
    EXPECT_EQ(r.value().find("Path:ssml"),    std::string::npos);
    EXPECT_EQ(r.value().find("Content-Type"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST(SsmlBuilder, OutputIsDeterministic) {
    const auto r1 = builder.build(TtsConfig::defaults(), "hello world");
    const auto r2 = builder.build(TtsConfig::defaults(), "hello world");
    EXPECT_TRUE(r1.has_value());
    EXPECT_TRUE(r2.has_value());
    EXPECT_EQ(r1.value(), r2.value());
}

// ---------------------------------------------------------------------------
// build() escaping contract: raw input escaped exactly once
// ---------------------------------------------------------------------------

TEST(SsmlBuilder, RawInputEscapedExactlyOnce) {
    // "Tom & Jerry <x>" raw → SSML must contain "Tom &amp; Jerry &lt;x&gt;" once.
    const auto r = builder.build(TtsConfig::defaults(), "Tom & Jerry <x>");
    EXPECT_TRUE(r.has_value());
    const auto& ssml = r.value();
    EXPECT_NE(ssml.find("Tom &amp; Jerry &lt;x&gt;"), std::string::npos);
    // Raw characters must not survive into the SSML.
    EXPECT_EQ(ssml.find("Tom & Jerry"), std::string::npos);
    // Double-escaping must not occur.
    EXPECT_EQ(ssml.find("&amp;amp;"), std::string::npos);
    EXPECT_EQ(ssml.find("&amp;lt;"),  std::string::npos);
}

// ---------------------------------------------------------------------------
// build_from_escaped_text(): pre-escaped input embedded verbatim
// ---------------------------------------------------------------------------

TEST(SsmlBuilder, PreEscapedInputEmbeddedVerbatim) {
    // Already-escaped "Tom &amp; Jerry" must remain "Tom &amp; Jerry" in the
    // SSML — it must NOT become "Tom &amp;amp; Jerry".
    const auto r = builder.build_from_escaped_text(
        TtsConfig::defaults(), "Tom &amp; Jerry");
    EXPECT_TRUE(r.has_value());
    const auto& ssml = r.value();
    EXPECT_NE(ssml.find("Tom &amp; Jerry"), std::string::npos);
    // No double-escaping.
    EXPECT_EQ(ssml.find("Tom &amp;amp; Jerry"), std::string::npos);
}

TEST(SsmlBuilder, PreEscapedInputAllEntitiesVerbatim) {
    // "Tom &amp; Jerry &lt;x&gt;" pre-escaped must appear unchanged.
    const auto r = builder.build_from_escaped_text(
        TtsConfig::defaults(), "Tom &amp; Jerry &lt;x&gt;");
    EXPECT_TRUE(r.has_value());
    const auto& ssml = r.value();
    EXPECT_NE(ssml.find("Tom &amp; Jerry &lt;x&gt;"), std::string::npos);
    EXPECT_EQ(ssml.find("&amp;amp;"), std::string::npos);
    EXPECT_EQ(ssml.find("&amp;lt;"),  std::string::npos);
}

TEST(SsmlBuilder, PreEscapedInvalidConfigReturnsError) {
    TtsConfig bad = TtsConfig::defaults();
    bad.voice = "not-a-valid-voice";
    const auto r = builder.build_from_escaped_text(bad, "Tom &amp; Jerry");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_argument);
}

TEST(SsmlBuilder, PreEscapedProducesStructurallyValidSsml) {
    const auto r = builder.build_from_escaped_text(
        TtsConfig::defaults(), "Hello &amp; world");
    EXPECT_TRUE(r.has_value());
    EXPECT_NE(r.value().find("<speak"),    std::string::npos);
    EXPECT_NE(r.value().find("<prosody"),  std::string::npos);
    EXPECT_NE(r.value().find("</speak>"), std::string::npos);
}
