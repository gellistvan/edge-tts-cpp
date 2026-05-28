#include "edge_tts/common/Hex.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <array>
#include <cstddef>

using edge_tts::common::hex_encode_lower;
using edge_tts::common::hex_encode_upper;
using edge_tts::common::is_hex;

static std::span<const std::byte> as_bytes(const std::array<unsigned char, 4>& a) {
    return {reinterpret_cast<const std::byte*>(a.data()), a.size()};
}

// ---------------------------------------------------------------------------
// hex_encode_lower
// ---------------------------------------------------------------------------

TEST(HexEncodeLower, KnownValues) {
    const std::array<unsigned char, 4> input{0x00, 0xff, 0xde, 0xad};
    EXPECT_EQ(hex_encode_lower(as_bytes(input)), "00ffdead");
}

TEST(HexEncodeLower, SingleByte) {
    const std::array<unsigned char, 1> b1{0x0a};
    EXPECT_EQ(hex_encode_lower({reinterpret_cast<const std::byte*>(b1.data()), 1}), "0a");
    const std::array<unsigned char, 1> b2{0xf0};
    EXPECT_EQ(hex_encode_lower({reinterpret_cast<const std::byte*>(b2.data()), 1}), "f0");
}

TEST(HexEncodeLower, Empty) {
    EXPECT_EQ(hex_encode_lower({}), "");
}

TEST(HexEncodeLower, OutputLength) {
    const std::array<unsigned char, 16> buf{};
    EXPECT_EQ(hex_encode_lower({reinterpret_cast<const std::byte*>(buf.data()), 16}).size(), 32u);
}

TEST(HexEncodeLower, AllLowercase) {
    const std::array<unsigned char, 6> input{0xab, 0xcd, 0xef, 0x01, 0x23, 0x45};
    const auto s = hex_encode_lower({reinterpret_cast<const std::byte*>(input.data()), 6});
    EXPECT_EQ(s, "abcdef012345");
    // Verify no uppercase characters
    for (const char c : s) {
        EXPECT_FALSE(c >= 'A' && c <= 'F');
    }
}

// ---------------------------------------------------------------------------
// hex_encode_upper
// ---------------------------------------------------------------------------

TEST(HexEncodeUpper, KnownValues) {
    const std::array<unsigned char, 4> input{0xde, 0xad, 0xbe, 0xef};
    EXPECT_EQ(hex_encode_upper(as_bytes(input)), "DEADBEEF");
}

TEST(HexEncodeUpper, AllUppercase) {
    const std::array<unsigned char, 6> input{0xab, 0xcd, 0xef, 0x01, 0x23, 0x45};
    const auto s = hex_encode_upper({reinterpret_cast<const std::byte*>(input.data()), 6});
    EXPECT_EQ(s, "ABCDEF012345");
    for (const char c : s) {
        EXPECT_FALSE(c >= 'a' && c <= 'f');
    }
}

TEST(HexEncodeUpper, Empty) {
    EXPECT_EQ(hex_encode_upper({}), "");
}

// Lower and upper encode the same bytes differently
TEST(HexEncode, LowerAndUpperDifferForLetters) {
    const std::array<unsigned char, 1> b{0xab};
    const auto span = std::span<const std::byte>{reinterpret_cast<const std::byte*>(b.data()), 1};
    EXPECT_NE(hex_encode_lower(span), hex_encode_upper(span));
    EXPECT_EQ(hex_encode_lower(span), "ab");
    EXPECT_EQ(hex_encode_upper(span), "AB");
}

// ---------------------------------------------------------------------------
// is_hex
// ---------------------------------------------------------------------------

TEST(IsHex, ValidLowercase) {
    EXPECT_TRUE(is_hex("0123456789abcdef"));
    EXPECT_TRUE(is_hex("deadbeef"));
    EXPECT_TRUE(is_hex("00"));
    EXPECT_TRUE(is_hex("f"));
}

TEST(IsHex, ValidUppercase) {
    EXPECT_TRUE(is_hex("DEADBEEF"));
    EXPECT_TRUE(is_hex("0123456789ABCDEF"));
}

TEST(IsHex, ValidMixed) {
    EXPECT_TRUE(is_hex("DeAdBeEf"));
}

TEST(IsHex, Empty) {
    EXPECT_FALSE(is_hex(""));
}

TEST(IsHex, InvalidChars) {
    EXPECT_FALSE(is_hex("g"));
    EXPECT_FALSE(is_hex("zz"));
    EXPECT_FALSE(is_hex("0x1a"));  // "0x" prefix not valid hex chars
    EXPECT_FALSE(is_hex("dead beef"));  // space
    EXPECT_FALSE(is_hex("de-ad"));      // hyphen
}

TEST(IsHex, UUIDWithHyphensNotHex) {
    // A UUID with hyphens contains '-' which is not a hex digit
    EXPECT_FALSE(is_hex("550e8400-e29b-41d4-a716-446655440000"));
}

TEST(IsHex, UUIDWithoutHyphensIsHex) {
    EXPECT_TRUE(is_hex("550e8400e29b41d4a716446655440000"));
}
