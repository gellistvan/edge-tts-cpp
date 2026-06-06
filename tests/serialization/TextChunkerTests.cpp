#include "edge_tts/serialization/TextChunker.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <string>
#include <vector>

using edge_tts::serialization::TextChunker;
using edge_tts::serialization::TextChunkerOptions;
using edge_tts::common::ErrorCode;

// Helper: build a chunker for tests (reference-mode unless overridden).
static TextChunker ref(std::size_t lim,
                        bool after_escape = true,
                        bool newline = true,
                        bool space = true) {
    return TextChunker{TextChunkerOptions{lim, after_escape, newline, space}};
}

// ---------------------------------------------------------------------------
// Constructor / configuration errors
// ---------------------------------------------------------------------------

TEST(TextChunker, ZeroMaxChunkSizeThrows) {
    EXPECT_THROW(
        (TextChunker{TextChunkerOptions{0, true, true, true}}),
        std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Empty / whitespace-only input
// ---------------------------------------------------------------------------

TEST(TextChunker, EmptyInputReturnsNoChunks) {
    const auto r = ref(10).chunk("");
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

TEST(TextChunker, WhitespaceOnlyInputReturnsNoChunks) {
    // After normalization, escaping, and stripping, pure whitespace yields nothing.
    const auto r = ref(10).chunk("   \n\t  ");
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

// ---------------------------------------------------------------------------
// Short input — fits in one chunk
// ---------------------------------------------------------------------------

TEST(TextChunker, ShortInputIsOneChunk) {
    const auto r = ref(20).chunk("hello world");
    EXPECT_TRUE(r.has_value());
    ASSERT_EQ(r.value().size(), 1u);
    EXPECT_EQ(r.value()[0], "hello world");
}

TEST(TextChunker, ExactlyAtLimitIsOneChunk) {
    // "1234567890" is exactly 10 bytes; limit = 10.
    const auto r = ref(10).chunk("1234567890");
    EXPECT_TRUE(r.has_value());
    ASSERT_EQ(r.value().size(), 1u);
    EXPECT_EQ(r.value()[0], "1234567890");
}

// ---------------------------------------------------------------------------
// Split at space (word boundary)
// ---------------------------------------------------------------------------

TEST(TextChunker, ExceedsLimitSplitsAtSpace) {
    // "hello world" (11 bytes) with limit 8.
    // rfind('\n', 7) = npos; rfind(' ', 7) = 5 (space between hello/world).
    // Chunk 1: strip("hello") = "hello"; remainder: strip(" world") = "world".
    const auto r = ref(8).chunk("hello world");
    EXPECT_TRUE(r.has_value());
    ASSERT_EQ(r.value().size(), 2u);
    EXPECT_EQ(r.value()[0], "hello");
    EXPECT_EQ(r.value()[1], "world");
}

TEST(TextChunker, LeadingAndTrailingWhitespaceStrippedFromChunks) {
    // "  abc def  " (11 bytes) with limit 7.
    // rfind(' ', 6) in "  abc d" → 6.  strip("  abc d") → strip gives "  abc d"
    // Actually: rfind(' ', 6) = 6. chunk = strip("  abc d") = "abc d". Nope wait:
    // chunk = strip(text[0..6]) = strip("  abc d") = "abc d".
    // Then text = text[6..] = " def  " (6 bytes <= 7). remainder = strip(" def  ") = "def".
    // Hmm but "  abc d" rfind(' ', 6) = 6 (the last ' ' in positions 0-6 is at index 6).
    // Let me trace: "  abc def  " = ' ',' ','a','b','c',' ','d','e','f',' ',' '
    //                               0   1   2   3   4   5   6   7   8   9  10
    // limit = 7. rfind(' ', 6) = 5. chunk = strip("  abc") = "abc". text = " def  " (6 <= 7).
    // remainder = "def".
    const auto r = ref(7).chunk("  abc def  ");
    EXPECT_TRUE(r.has_value());
    ASSERT_EQ(r.value().size(), 2u);
    EXPECT_EQ(r.value()[0], "abc");
    EXPECT_EQ(r.value()[1], "def");
}

// ---------------------------------------------------------------------------
// Split at newline (sentence/paragraph boundary, preferred over space)
// ---------------------------------------------------------------------------

TEST(TextChunker, NewlinePreferredOverSpace) {
    // "fo bar\nbaz" (10 bytes) with limit 7.
    //  f=0,o=1,' '=2,b=3,a=4,r=5,'\n'=6,b=7,a=8,z=9
    // rfind('\n', 6) = 6 → split at newline, NOT at space (pos 2).
    // chunk = strip("fo bar") = "fo bar"; text = "\nbaz" (4 <= 7). remainder = "baz".
    const auto r = ref(7).chunk("fo bar\nbaz");
    EXPECT_TRUE(r.has_value());
    ASSERT_EQ(r.value().size(), 2u);
    EXPECT_EQ(r.value()[0], "fo bar");
    EXPECT_EQ(r.value()[1], "baz");
}

TEST(TextChunker, WithoutNewlinePreferenceSplitsAtSpace) {
    // Same input as above but prefer_sentence_boundary = false.
    // rfind(' ', 6) = 2 → splits at "fo".
    // "fo" | " bar\nbaz" (8 bytes > 7) → rfind(' ', 6) in " bar\nb" = 0 → empty/skip, advance
    // → "bar\nbaz" (7 <= 7) → remainder = "bar\nbaz"
    // Wait: " bar\nbaz"[0..6] rfind(' ', 6) = 0 (space at 0). split_at = 0 → skip, advance by 1.
    // text = "bar\nbaz" (7 <= 7). remainder = "bar\nbaz".
    const auto r = ref(7, true, false, true).chunk("fo bar\nbaz");
    EXPECT_TRUE(r.has_value());
    ASSERT_EQ(r.value().size(), 2u);
    EXPECT_EQ(r.value()[0], "fo");
    EXPECT_EQ(r.value()[1], "bar\nbaz");
}

// ---------------------------------------------------------------------------
// Long word — hard split at UTF-8 boundary (no space/newline)
// ---------------------------------------------------------------------------

TEST(TextChunker, LongWordHardSplitAtByteBoundary) {
    // "ABCDEFGHIJ" (10 ASCII bytes) with limit 4.
    // No \n or space → UTF-8 safe split at limit = 4 each time.
    const auto r = ref(4).chunk("ABCDEFGHIJ");
    EXPECT_TRUE(r.has_value());
    ASSERT_EQ(r.value().size(), 3u);
    EXPECT_EQ(r.value()[0], "ABCD");
    EXPECT_EQ(r.value()[1], "EFGH");
    EXPECT_EQ(r.value()[2], "IJ");
}

// ---------------------------------------------------------------------------
// Emoji: never split inside a 4-byte code point
// ---------------------------------------------------------------------------

TEST(TextChunker, EmojiNotSplitInMiddle) {
    // "ab" + 😀 (U+1F600, 4 bytes: \xF0\x9F\x98\x80) = 6 bytes total.
    // limit = 4: rfind → npos; UTF-8 split starts at 4, walks back past
    // 3 continuation bytes to position 2 (the lead \xF0). split_at = 2.
    // chunk "ab", remainder "😀".
    const std::string input = "ab\xF0\x9F\x98\x80";
    const auto r = ref(4).chunk(input);
    EXPECT_TRUE(r.has_value());
    ASSERT_EQ(r.value().size(), 2u);
    EXPECT_EQ(r.value()[0], "ab");
    EXPECT_EQ(r.value()[1], "\xF0\x9F\x98\x80");
}

// ---------------------------------------------------------------------------
// CJK: 3-byte characters, split at a code-point boundary
// ---------------------------------------------------------------------------

TEST(TextChunker, CJKSplitAtCodePointBoundary) {
    // Three copies of 中 (U+4E2D, \xE4\xB8\xAD, 3 bytes each) = 9 bytes.
    // limit = 7: UTF-8 split starts at 7; text[7]=\xAD (continuation) → 6;
    // text[6]=\xE4 (lead, not continuation) → stop. split_at = 6.
    // chunk = "中中" (6 bytes), remainder = "中".
    const std::string cjk3 = "\xE4\xB8\xAD";
    const auto r = ref(7).chunk(cjk3 + cjk3 + cjk3);
    EXPECT_TRUE(r.has_value());
    ASSERT_EQ(r.value().size(), 2u);
    EXPECT_EQ(r.value()[0], cjk3 + cjk3);
    EXPECT_EQ(r.value()[1], cjk3);
}

// ---------------------------------------------------------------------------
// XML escaping affects effective chunk size
// ---------------------------------------------------------------------------

TEST(TextChunker, XmlEscapeInflatesSize) {
    // "a&b" raw = 3 bytes.  Escaped = "a&amp;b" = 7 bytes.
    // size_after_xml_escape = true, limit = 5:
    //   7 > 5; rfind('\n', 4)=npos; rfind(' ', 4)=npos; UTF-8 split at 5;
    //   text[5]=';' not continuation → split_at = 5.
    //   XML adj: rfind('&', 4) = 1; find(';', 2, 5): "amp" at [2..4] — no ';'. → split_at = 1.
    //   chunk = strip("a") = "a"; text = "&amp;b" (6 > 5).
    //   rfind npos; split_at = 5; text[5]='b' → 5. XML adj: rfind('&',4)=0; find(';',1,5)=4<5 → complete.
    //   chunk = "&amp;"; remainder = "b".
    const auto r = ref(5).chunk("a&b");
    EXPECT_TRUE(r.has_value());
    ASSERT_EQ(r.value().size(), 3u);
    EXPECT_EQ(r.value()[0], "a");
    EXPECT_EQ(r.value()[1], "&amp;");
    EXPECT_EQ(r.value()[2], "b");
}

TEST(TextChunker, RawSizeModeFitsWithoutSplitting) {
    // "a&b" raw = 3 bytes.  size_after_xml_escape = false, limit = 5:
    // raw "a&b" (3 bytes) fits in 5 → one raw chunk → xml_escape → "a&amp;b".
    const auto r = ref(5, false).chunk("a&b");
    EXPECT_TRUE(r.has_value());
    ASSERT_EQ(r.value().size(), 1u);
    EXPECT_EQ(r.value()[0], "a&amp;b");
}

// ---------------------------------------------------------------------------
// XML entity protection — never split inside &…;
// ---------------------------------------------------------------------------

TEST(TextChunker, XmlEntityNotSplitInMiddle) {
    // "ab<cd" raw; escaped = "ab&lt;cd" (8 bytes).
    //  a=0,b=1,&=2,l=3,t=4,;=5,c=6,d=7.  limit = 5.
    // rfind npos; split_at = 5; text[5]=';' not continuation → 5.
    // XML adj: rfind('&', 4)=2; find(';', 3, 5): searching [3,4]="lt" — no ';'. → split_at = 2.
    // chunk = "ab"; text = "&lt;cd" (6 > 5).
    // rfind npos; split_at = 5; text[5]='d' → 5.
    // XML adj: rfind('&', 4)=0; find(';', 1, 5): "&lt;c"[1..4]="lt;c", ';' at 3 < 5 → complete.
    // chunk = "&lt;c"; remainder = "d".
    const auto r = ref(5).chunk("ab<cd");
    EXPECT_TRUE(r.has_value());
    ASSERT_EQ(r.value().size(), 3u);
    EXPECT_EQ(r.value()[0], "ab");
    EXPECT_EQ(r.value()[1], "&lt;c");
    EXPECT_EQ(r.value()[2], "d");
}

// ---------------------------------------------------------------------------
// XML entity split boundary regression — chunks never split inside &amp; or &lt;
// ---------------------------------------------------------------------------

TEST(TextChunker, AmpEntityNotSplitInMiddle) {
    // "a&b" raw → escaped "a&amp;b" (7 bytes).  With limit=5 the chunker must
    // not split "a&amp;b" into "a&amp" + ";b" — it must protect the entity.
    // Expected splits: "a" | "&amp;" | "b" (entity kept intact).
    const auto r = ref(5).chunk("a&b");
    EXPECT_TRUE(r.has_value());
    // No chunk may contain a partial entity like "&amp" (no semicolon) or
    // start with ";b" (orphaned semicolon).
    for (const auto& chunk : r.value()) {
        // A bare & without a closing ; is a broken entity.
        const auto amp_pos = chunk.find('&');
        if (amp_pos != std::string::npos) {
            EXPECT_NE(chunk.find(';', amp_pos), std::string::npos);
        }
    }
    // Verify the chunks together contain exactly "a", "&amp;", "b".
    ASSERT_EQ(r.value().size(), 3u);
    EXPECT_EQ(r.value()[0], "a");
    EXPECT_EQ(r.value()[1], "&amp;");
    EXPECT_EQ(r.value()[2], "b");
}

TEST(TextChunker, LtEntityNotSplitInMiddle) {
    // "a<b" raw → escaped "a&lt;b" (6 bytes).  With limit=4 the entity must
    // not be split: splits must be "a" | "&lt;" | "b" (5 bytes, then 1 byte).
    // Actually limit=4: "a&lt;b" → "a" (split at 1 because XML adj) | "&lt;b" (5 > 4) →
    // "&lt;" | "b".
    const auto r = ref(4).chunk("a<b");
    EXPECT_TRUE(r.has_value());
    for (const auto& chunk : r.value()) {
        const auto amp_pos = chunk.find('&');
        if (amp_pos != std::string::npos) {
            EXPECT_NE(chunk.find(';', amp_pos), std::string::npos);
        }
    }
}

// ---------------------------------------------------------------------------
// Control character replacement (via TextNormalizer)
// ---------------------------------------------------------------------------

TEST(TextChunker, ControlCharsReplacedBeforeChunking) {
    // VT (\x0B) is replaced by space by TextNormalizer before escaping.
    const auto r = ref(20).chunk("hello\x0Bworld");
    EXPECT_TRUE(r.has_value());
    ASSERT_EQ(r.value().size(), 1u);
    EXPECT_EQ(r.value()[0], "hello world");
}

// ---------------------------------------------------------------------------
// Error propagation — invalid UTF-8
// ---------------------------------------------------------------------------

TEST(TextChunker, InvalidUtf8PropagatesError) {
    const auto r = ref(10).chunk("\x80");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_argument);
}

// ---------------------------------------------------------------------------
// Determinism — same input always produces identical output
// ---------------------------------------------------------------------------

TEST(TextChunker, OutputIsDeterministic) {
    const auto r1 = ref(8).chunk("hello world, how are you?");
    const auto r2 = ref(8).chunk("hello world, how are you?");
    EXPECT_TRUE(r1.has_value());
    EXPECT_TRUE(r2.has_value());
    EXPECT_EQ(r1.value(), r2.value());
}

// ---------------------------------------------------------------------------
// Reference constants sanity check
// ---------------------------------------------------------------------------

TEST(TextChunker, DefaultOptionsMatch4096Limit) {
    // Default TextChunkerOptions must reflect the reference pipeline values.
    const TextChunkerOptions opts{};
    EXPECT_EQ(opts.max_chunk_size, 4096u);
    EXPECT_TRUE(opts.size_after_xml_escape);
    EXPECT_TRUE(opts.prefer_sentence_boundary);
    EXPECT_TRUE(opts.prefer_word_boundary);
}
