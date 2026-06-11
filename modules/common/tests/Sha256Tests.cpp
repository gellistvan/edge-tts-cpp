#include "common/Sha256.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <string>

using edge_tts::common::sha256_hex;
using edge_tts::common::sha256_hex_upper;

// All expected values verified against the system sha256sum utility.

// ---------------------------------------------------------------------------
// sha256_hex — lowercase output
// ---------------------------------------------------------------------------

TEST(Sha256, EmptyString) {
    // sha256sum: echo -n "" | sha256sum
    EXPECT_EQ(sha256_hex(""),
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256, Abc) {
    // sha256sum: echo -n "abc" | sha256sum
    EXPECT_EQ(sha256_hex("abc"),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256, OutputLength) {
    EXPECT_EQ(sha256_hex("").size(), 64u);
    EXPECT_EQ(sha256_hex("abc").size(), 64u);
    EXPECT_EQ(sha256_hex(std::string(1024, 'x')).size(), 64u);
}

TEST(Sha256, OutputIsLowercase) {
    const std::string h = sha256_hex("abc");
    for (const char c : h) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        EXPECT_TRUE(ok);
    }
}

// ---------------------------------------------------------------------------
// sha256_hex_upper — uppercase output
// ---------------------------------------------------------------------------

TEST(Sha256, UppercaseEmptyString) {
    EXPECT_EQ(sha256_hex_upper(""),
        "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855");
}

TEST(Sha256, UppercaseAbc) {
    EXPECT_EQ(sha256_hex_upper("abc"),
        "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD");
}

TEST(Sha256, UppercaseOutputIsUppercase) {
    const std::string h = sha256_hex_upper("abc");
    for (const char c : h) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
        EXPECT_TRUE(ok);
    }
}

// ---------------------------------------------------------------------------
// Known-answer test for the Sec-MS-GEC token input at unix timestamp = 0
//   str_to_hash = "1164447360000000006A5AA1D4EAFF4E9FB37E23D68491D6F4"
//   expected hash = "7ECB79D14E3AA576D2D79E6D487A1388156D91E614B1BE11C64226A29BC8DD8C"
// Verified: echo -n "1164447360000000006A5AA1D4EAFF4E9FB37E23D68491D6F4" | sha256sum
// ---------------------------------------------------------------------------

TEST(Sha256, TokenInputVector) {
    EXPECT_EQ(
        sha256_hex_upper("1164447360000000006A5AA1D4EAFF4E9FB37E23D68491D6F4"),
        "7ECB79D14E3AA576D2D79E6D487A1388156D91E614B1BE11C64226A29BC8DD8C");
}

TEST(Sha256, LargeInput) {
    // Smoke test: ensure multi-block input is handled correctly.
    // 128 'a' characters requires 3 SHA-256 blocks (including padding).
    const std::string expected_of_128a = []() {
        // Computed offline: echo -n "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" | sha256sum
        // (128 'a' characters)
        return std::string{"6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e"};
    }();
    // Just verify length and that it doesn't crash — exact value depends on correct implementation.
    const std::string result = sha256_hex(std::string(128, 'a'));
    EXPECT_EQ(result.size(), 64u);
    EXPECT_EQ(result, expected_of_128a);
}

TEST(Sha256, SingleByteInput) {
    // sha256sum: printf '\x61' | sha256sum (same as "a")
    EXPECT_EQ(sha256_hex("a"),
        "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb");
}
