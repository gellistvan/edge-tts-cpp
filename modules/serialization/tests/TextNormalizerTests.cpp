#include "serialization/TextNormalizer.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <string>

using edge_tts::serialization::TextNormalizer;
using edge_tts::common::ErrorCode;

static TextNormalizer normalizer{};

// ---------------------------------------------------------------------------
// Valid UTF-8 — passes through with control-char replacement
// ---------------------------------------------------------------------------

TEST(TextNormalizer, EmptyInputOk) {
    const auto r = normalizer.normalize("");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), "");
}

TEST(TextNormalizer, PlainASCIIUnchanged) {
    const auto r = normalizer.normalize("Hello, world!");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), "Hello, world!");
}

TEST(TextNormalizer, TabPreserved) {
    // U+0009 (tab) must NOT be replaced
    const auto r = normalizer.normalize("a\tb");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), "a\tb");
}

TEST(TextNormalizer, LFPreserved) {
    // U+000A (LF) must NOT be replaced
    const auto r = normalizer.normalize("line1\nline2");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), "line1\nline2");
}

TEST(TextNormalizer, CRPreserved) {
    // U+000D (CR) must NOT be replaced
    const auto r = normalizer.normalize("a\rb");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), "a\rb");
}

TEST(TextNormalizer, CRLFPreservedNotNormalised) {
    // CRLF is preserved intact — Python does NOT convert \r\n to \n
    const auto r = normalizer.normalize("a\r\nb");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), "a\r\nb");
}

TEST(TextNormalizer, SpacesPreserved) {
    // Leading, trailing, and internal spaces are NOT trimmed
    const auto r = normalizer.normalize("  hello  world  ");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), "  hello  world  ");
}

// ---------------------------------------------------------------------------
// Control character replacement (matching Python ranges)
// ---------------------------------------------------------------------------

TEST(TextNormalizer, NulBecomesSpace) {
    // U+0000
    const std::string in{"\x00", 1};
    const auto r = normalizer.normalize(in);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), " ");
}

TEST(TextNormalizer, BELBecomesSpace) {
    // U+0007 (BEL) — in 0-8 range
    const auto r = normalizer.normalize("\x07");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), " ");
}

TEST(TextNormalizer, VerticalTabBecomesSpace) {
    // U+000B (VT) — in 11-12 range
    const auto r = normalizer.normalize("\x0B");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), " ");
}

TEST(TextNormalizer, FormFeedBecomesSpace) {
    // U+000C (FF) — in 11-12 range
    const auto r = normalizer.normalize("\x0C");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), " ");
}

TEST(TextNormalizer, ShiftOutBecomesSpace) {
    // U+000E (SO) — in 14-31 range
    const auto r = normalizer.normalize("\x0E");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), " ");
}

TEST(TextNormalizer, US31BecomesSpace) {
    // U+001F (US) — last value in 14-31 range
    const auto r = normalizer.normalize("\x1F");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), " ");
}

TEST(TextNormalizer, MixedControlAndPrintable) {
    // Only the VT in the middle should be replaced
    const auto r = normalizer.normalize("hello\x0Bworld");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), "hello world");
}

TEST(TextNormalizer, SpaceU0020NotReplaced) {
    // U+0020 is the FIRST printable character; must be preserved
    const auto r = normalizer.normalize(" a ");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), " a ");
}

// ---------------------------------------------------------------------------
// Unicode preservation
// ---------------------------------------------------------------------------

TEST(TextNormalizer, AccentedCharsPreserved) {
    // é = U+00E9 (2-byte)
    const auto r = normalizer.normalize("caf\xC3\xA9");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), "caf\xC3\xA9");
}

TEST(TextNormalizer, CJKPreserved) {
    // 中 = U+4E2D (3-byte)
    const auto r = normalizer.normalize("\xE4\xB8\xAD");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), "\xE4\xB8\xAD");
}

TEST(TextNormalizer, EmojiPreserved) {
    // 😀 = U+1F600 (4-byte)
    const auto r = normalizer.normalize("\xF0\x9F\x98\x80");
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), "\xF0\x9F\x98\x80");
}

// ---------------------------------------------------------------------------
// Invalid UTF-8 — must be rejected
// ---------------------------------------------------------------------------

TEST(TextNormalizer, InvalidContinuationByteRejected) {
    const auto r = normalizer.normalize("\x80");
    EXPECT_FALSE(r.has_value());
    EXPECT_TRUE(r.error().code() == ErrorCode::invalid_argument);
}

TEST(TextNormalizer, TruncatedSequenceRejected) {
    const auto r = normalizer.normalize("\xC3");  // start of é, no continuation
    EXPECT_FALSE(r.has_value());
    EXPECT_TRUE(r.error().code() == ErrorCode::invalid_argument);
}

TEST(TextNormalizer, SurrogateRejected) {
    // U+D800 encoded as UTF-8: 0xED 0xA0 0x80
    const auto r = normalizer.normalize("\xED\xA0\x80");
    EXPECT_FALSE(r.has_value());
    EXPECT_TRUE(r.error().code() == ErrorCode::invalid_argument);
}
