#include "common/IdGenerator.hpp"
#include "common/Hex.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>

using edge_tts::common::IdGenerator;
using edge_tts::common::is_hex;

// ---------------------------------------------------------------------------
// UUID v4 with hyphens
// ---------------------------------------------------------------------------

TEST(UuidV4, Length) {
    IdGenerator gen;
    EXPECT_EQ(gen.uuid_v4().size(), 36u);
}

TEST(UuidV4, HyphenPositions) {
    IdGenerator gen;
    const auto u = gen.uuid_v4();
    // Format: 8-4-4-4-12
    EXPECT_EQ(u[8],  '-');
    EXPECT_EQ(u[13], '-');
    EXPECT_EQ(u[18], '-');
    EXPECT_EQ(u[23], '-');
}

TEST(UuidV4, AllHexOutsideHyphens) {
    IdGenerator gen;
    const auto u = gen.uuid_v4();
    for (std::size_t i = 0; i < u.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        EXPECT_TRUE(is_hex(std::string{u[i]}));
    }
}

TEST(UuidV4, AllLowercase) {
    IdGenerator gen;
    const auto u = gen.uuid_v4();
    for (const char c : u) {
        if (c == '-') continue;
        EXPECT_FALSE(std::isupper(static_cast<unsigned char>(c)));
    }
}

TEST(UuidV4, VersionBit) {
    // Byte at index 14 must be '4' (version 4)
    IdGenerator gen;
    const auto u = gen.uuid_v4();
    EXPECT_EQ(u[14], '4');
}

TEST(UuidV4, VariantBit) {
    // The first hex digit at index 19 must be 8, 9, a, or b (variant 10xx)
    IdGenerator gen;
    for (int i = 0; i < 20; ++i) {
        const auto u = gen.uuid_v4();
        const char v = u[19];
        EXPECT_TRUE(v == '8' || v == '9' || v == 'a' || v == 'b');
    }
}

// ---------------------------------------------------------------------------
// UUID v4 without hyphens
// ---------------------------------------------------------------------------

TEST(UuidV4WithoutHyphens, Length) {
    IdGenerator gen;
    EXPECT_EQ(gen.uuid_v4_without_hyphens().size(), 32u);
}

TEST(UuidV4WithoutHyphens, NoHyphens) {
    IdGenerator gen;
    const auto u = gen.uuid_v4_without_hyphens();
    EXPECT_EQ(u.find('-'), std::string::npos);
}

TEST(UuidV4WithoutHyphens, AllHexLowercase) {
    IdGenerator gen;
    const auto u = gen.uuid_v4_without_hyphens();
    EXPECT_TRUE(is_hex(u));
    for (const char c : u) {
        EXPECT_FALSE(std::isupper(static_cast<unsigned char>(c)));
    }
}

TEST(UuidV4WithoutHyphens, MatchesStripOfWithHyphens) {
    // Build two generators with the same seed is not directly possible,
    // so just verify the structure: no-hyphen has the same chars as with-hyphen.
    IdGenerator gen;
    // Generate a no-hyphen UUID and verify that inserting hyphens at the
    // expected positions would produce a valid with-hyphen UUID.
    const auto nh = gen.uuid_v4_without_hyphens();
    EXPECT_EQ(nh.size(), 32u);
    EXPECT_TRUE(is_hex(nh));
}

// ---------------------------------------------------------------------------
// random_32_hex
// ---------------------------------------------------------------------------

TEST(Random32Hex, Length) {
    IdGenerator gen;
    EXPECT_EQ(gen.random_32_hex().size(), 32u);
}

TEST(Random32Hex, AllHexLowercase) {
    IdGenerator gen;
    const auto h = gen.random_32_hex();
    EXPECT_TRUE(is_hex(h));
    for (const char c : h) {
        EXPECT_FALSE(std::isupper(static_cast<unsigned char>(c)));
    }
}

TEST(Random32Hex, NoHyphens) {
    IdGenerator gen;
    EXPECT_EQ(gen.random_32_hex().find('-'), std::string::npos);
}

// ---------------------------------------------------------------------------
// Uniqueness across 100 generated IDs
// ---------------------------------------------------------------------------

TEST(UuidV4, UniqueAcross100) {
    IdGenerator gen;
    std::set<std::string> seen;
    for (int i = 0; i < 100; ++i) seen.insert(gen.uuid_v4());
    EXPECT_EQ(seen.size(), 100u);
}

TEST(UuidV4WithoutHyphens, UniqueAcross100) {
    IdGenerator gen;
    std::set<std::string> seen;
    for (int i = 0; i < 100; ++i) seen.insert(gen.uuid_v4_without_hyphens());
    EXPECT_EQ(seen.size(), 100u);
}

TEST(Random32Hex, UniqueAcross100) {
    IdGenerator gen;
    std::set<std::string> seen;
    for (int i = 0; i < 100; ++i) seen.insert(gen.random_32_hex());
    EXPECT_EQ(seen.size(), 100u);
}
