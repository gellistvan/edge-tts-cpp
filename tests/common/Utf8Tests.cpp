#include "edge_tts/common/Utf8.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <string>
#include <string_view>

using namespace edge_tts::common::utf8;

// ---------------------------------------------------------------------------
// is_valid_utf8 — valid inputs
// ---------------------------------------------------------------------------

TEST(IsValidUtf8, EmptyStringIsValid) {
    EXPECT_TRUE(is_valid_utf8(""));
}

TEST(IsValidUtf8, ASCIIValid) {
    EXPECT_TRUE(is_valid_utf8("Hello, world!"));
    EXPECT_TRUE(is_valid_utf8("0123456789"));
    EXPECT_TRUE(is_valid_utf8("\t\n\r"));
}

TEST(IsValidUtf8, AccentedCharactersValid) {
    // é = U+00E9 = 0xC3 0xA9 (2-byte)
    EXPECT_TRUE(is_valid_utf8("\xC3\xA9"));
    // ñ = U+00F1 = 0xC3 0xB1 (2-byte)
    EXPECT_TRUE(is_valid_utf8("ma\xC3\xB1" "ana"));
    // ü = U+00FC = 0xC3 0xBC
    EXPECT_TRUE(is_valid_utf8("\xC3\xBC" "ber"));
}

TEST(IsValidUtf8, CJKValid) {
    // 中 = U+4E2D = 0xE4 0xB8 0xAD (3-byte)
    EXPECT_TRUE(is_valid_utf8("\xE4\xB8\xAD"));
    // 文 = U+6587 = 0xE6 0x96 0x87
    EXPECT_TRUE(is_valid_utf8("\xE6\x96\x87"));
    // 日本語
    EXPECT_TRUE(is_valid_utf8("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"));
}

TEST(IsValidUtf8, EmojiValid) {
    // 😀 = U+1F600 = 0xF0 0x9F 0x98 0x80 (4-byte)
    EXPECT_TRUE(is_valid_utf8("\xF0\x9F\x98\x80"));
    // 🌍 = U+1F30D = 0xF0 0x9F 0x8C 0x8D
    EXPECT_TRUE(is_valid_utf8("\xF0\x9F\x8C\x8D"));
}

TEST(IsValidUtf8, MaxValidCodePoint) {
    // U+10FFFF = 0xF4 0x8F 0xBF 0xBF
    EXPECT_TRUE(is_valid_utf8("\xF4\x8F\xBF\xBF"));
}

// ---------------------------------------------------------------------------
// is_valid_utf8 — invalid inputs
// ---------------------------------------------------------------------------

TEST(IsValidUtf8, InvalidContinuationByte) {
    // 0x80 as lead byte is a continuation byte → invalid
    EXPECT_FALSE(is_valid_utf8("\x80"));
    EXPECT_FALSE(is_valid_utf8("abc\x80"));
}

TEST(IsValidUtf8, TruncatedSequence) {
    // Start of 2-byte sequence with no continuation
    EXPECT_FALSE(is_valid_utf8("\xC3"));
    // Start of 3-byte sequence with only one continuation
    EXPECT_FALSE(is_valid_utf8("\xE4\xB8"));
    // Start of 4-byte sequence with only two continuations
    EXPECT_FALSE(is_valid_utf8("\xF0\x9F\x98"));
}

TEST(IsValidUtf8, InvalidContinuationInMiddle) {
    // Valid lead, then ASCII byte instead of continuation
    EXPECT_FALSE(is_valid_utf8("\xC3\x41"));     // 0x41 = 'A', not continuation
    EXPECT_FALSE(is_valid_utf8("\xE4\xB8\x41")); // 3rd byte must be continuation
}

TEST(IsValidUtf8, OverlongEncoding2Byte) {
    // 0xC0 0x80 = overlong encoding of U+0000
    EXPECT_FALSE(is_valid_utf8("\xC0\x80"));
    // 0xC1 0xBF = overlong encoding of U+007F
    EXPECT_FALSE(is_valid_utf8("\xC1\xBF"));
}

TEST(IsValidUtf8, OverlongEncoding3Byte) {
    // 0xE0 0x80 0x80 = overlong encoding of U+0000 as 3-byte
    EXPECT_FALSE(is_valid_utf8("\xE0\x80\x80"));
    // 0xE0 0x9F 0xBF = overlong encoding of U+07FF
    EXPECT_FALSE(is_valid_utf8("\xE0\x9F\xBF"));
}

TEST(IsValidUtf8, SurrogateCodePoints) {
    // U+D800 = 0xED 0xA0 0x80
    EXPECT_FALSE(is_valid_utf8("\xED\xA0\x80"));
    // U+DFFF = 0xED 0xBF 0xBF
    EXPECT_FALSE(is_valid_utf8("\xED\xBF\xBF"));
    // U+D83D (high surrogate of 😀 in UTF-16) should be rejected
    EXPECT_FALSE(is_valid_utf8("\xED\xA0\xBD"));
}

TEST(IsValidUtf8, AboveU10FFFF) {
    // 0xF5 0x80 0x80 0x80 would encode above U+10FFFF
    EXPECT_FALSE(is_valid_utf8("\xF5\x80\x80\x80"));
    // 0xF4 0x90 0x80 0x80 = U+110000
    EXPECT_FALSE(is_valid_utf8("\xF4\x90\x80\x80"));
}

TEST(IsValidUtf8, InvalidLeadByte) {
    EXPECT_FALSE(is_valid_utf8("\xFF"));
    EXPECT_FALSE(is_valid_utf8("\xFE"));
}

// ---------------------------------------------------------------------------
// previous_code_point_boundary
// ---------------------------------------------------------------------------

TEST(PreviousCodePointBoundary, ASCIIAtBoundary) {
    EXPECT_EQ(previous_code_point_boundary("abc", 0), 0u);
    EXPECT_EQ(previous_code_point_boundary("abc", 1), 1u);
    EXPECT_EQ(previous_code_point_boundary("abc", 2), 2u);
}

TEST(PreviousCodePointBoundary, InsideMultibyte) {
    // "é" = 0xC3 0xA9, 2 bytes
    const std::string s = "\xC3\xA9";
    // Index 1 is the continuation byte; boundary should be 0 (start of é)
    EXPECT_EQ(previous_code_point_boundary(s, 1), 0u);
    EXPECT_EQ(previous_code_point_boundary(s, 0), 0u);
}

TEST(PreviousCodePointBoundary, Inside3ByteChar) {
    // 中 = 0xE4 0xB8 0xAD
    const std::string s = "\xE4\xB8\xAD";
    EXPECT_EQ(previous_code_point_boundary(s, 2), 0u);
    EXPECT_EQ(previous_code_point_boundary(s, 1), 0u);
    EXPECT_EQ(previous_code_point_boundary(s, 0), 0u);
}

TEST(PreviousCodePointBoundary, BeyondEnd) {
    const std::string s = "ab";
    EXPECT_EQ(previous_code_point_boundary(s, 10), s.size());
}

// ---------------------------------------------------------------------------
// next_code_point_boundary
// ---------------------------------------------------------------------------

TEST(NextCodePointBoundary, ASCII) {
    EXPECT_EQ(next_code_point_boundary("abc", 0), 1u);
    EXPECT_EQ(next_code_point_boundary("abc", 1), 2u);
    EXPECT_EQ(next_code_point_boundary("abc", 2), 3u);
}

TEST(NextCodePointBoundary, Past2ByteChar) {
    // "éb": é is 2 bytes, b is 1 byte
    const std::string s = "\xC3\xA9" "b";
    EXPECT_EQ(next_code_point_boundary(s, 0), 2u); // skip é, land on b
    EXPECT_EQ(next_code_point_boundary(s, 2), 3u); // skip b
    EXPECT_EQ(next_code_point_boundary(s, 3), 3u); // at end
}

TEST(NextCodePointBoundary, Past4ByteChar) {
    // 😀 = 4 bytes
    const std::string s = "\xF0\x9F\x98\x80";
    EXPECT_EQ(next_code_point_boundary(s, 0), 4u);
    EXPECT_EQ(next_code_point_boundary(s, 4), 4u); // at end
}

// ---------------------------------------------------------------------------
// split_utf8_by_byte_limit
// ---------------------------------------------------------------------------

TEST(SplitUtf8ByByteLimit, ZeroMaxBytesThrows) {
    EXPECT_THROW(split_utf8_by_byte_limit("abc", 0), std::invalid_argument);
}

TEST(SplitUtf8ByByteLimit, EmptyInput) {
    EXPECT_TRUE(split_utf8_by_byte_limit("", 4).empty());
}

TEST(SplitUtf8ByByteLimit, FitsInOneChunk) {
    const auto chunks = split_utf8_by_byte_limit("hello", 10);
    EXPECT_EQ(chunks.size(), 1u);
    EXPECT_EQ(chunks[0], "hello");
}

TEST(SplitUtf8ByByteLimit, ASCIIExactBoundary) {
    // "abcdef" split at 3: ["abc", "def"]
    const auto chunks = split_utf8_by_byte_limit("abcdef", 3);
    EXPECT_EQ(chunks.size(), 2u);
    EXPECT_EQ(chunks[0], "abc");
    EXPECT_EQ(chunks[1], "def");
}

TEST(SplitUtf8ByByteLimit, DoesNotBreakEmoji) {
    // 😀 is 4 bytes.  With max_bytes=3, the emoji must go to the second chunk.
    const std::string text = "ab\xF0\x9F\x98\x80";  // "ab😀" = 6 bytes
    const auto chunks = split_utf8_by_byte_limit(text, 4);
    // First chunk: "ab" (2 bytes, can't include the 4-byte emoji without
    // exceeding 4).  Actually "ab" = 2 bytes, emoji = 4 bytes.  With limit=4
    // the first chunk can be "ab" (2 bytes) then "😀" (4 bytes).
    // Or possibly chunk "ab" then "😀".
    // Let's verify the emoji is not split.
    for (const auto& chunk : chunks) {
        EXPECT_TRUE(is_valid_utf8(chunk));
    }
    // Verify full text is reconstructable
    std::string joined;
    for (const auto& c : chunks) joined += c;
    EXPECT_EQ(joined, text);
}

TEST(SplitUtf8ByByteLimit, SplitInsideMultibyteMovesToSafeBoundary) {
    // "a中b" = 1 + 3 + 1 = 5 bytes
    // With max_bytes=2: split at byte 2 is inside '中' (3 bytes), so we must
    // back up to after 'a' (byte 1).
    const std::string text = "a\xE4\xB8\xAD" "b";  // a + 中 + b
    const auto chunks = split_utf8_by_byte_limit(text, 2);
    for (const auto& chunk : chunks) {
        EXPECT_TRUE(is_valid_utf8(chunk));
    }
    std::string joined;
    for (const auto& c : chunks) joined += c;
    EXPECT_EQ(joined, text);
}

TEST(SplitUtf8ByByteLimit, CJKCharactersAroundLimit) {
    // "中文" = 6 bytes total.  Split at 4 → first chunk "中" (3 bytes),
    // second chunk "文" (3 bytes).
    const std::string text = "\xE4\xB8\xAD\xE6\x96\x87";
    const auto chunks = split_utf8_by_byte_limit(text, 4);
    EXPECT_EQ(chunks.size(), 2u);
    EXPECT_EQ(chunks[0], "\xE4\xB8\xAD");
    EXPECT_EQ(chunks[1], "\xE6\x96\x87");
    for (const auto& c : chunks) EXPECT_TRUE(is_valid_utf8(c));
}

TEST(SplitUtf8ByByteLimit, ReconstructsOriginal) {
    // Property: joining all chunks gives back the original text.
    const std::string text = "Hello, \xE4\xB8\x96\xE7\x95\x8C\xF0\x9F\x98\x80!";
    for (std::size_t limit : {1u, 2u, 3u, 4u, 5u, 10u, 100u}) {
        const auto chunks = split_utf8_by_byte_limit(text, limit);
        std::string joined;
        for (const auto& c : chunks) {
            joined += c;
            EXPECT_TRUE(is_valid_utf8(c));
        }
        EXPECT_EQ(joined, text);
    }
}
