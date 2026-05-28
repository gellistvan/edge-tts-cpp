#include "edge_tts/core/Voice.hpp"
#include "vendor/minigtest/minigtest.hpp"

using edge_tts::core::Voice;
using edge_tts::core::VoiceGender;

// ---------------------------------------------------------------------------
// VoiceGender
// ---------------------------------------------------------------------------
TEST(VoiceGender, ValuesAreDistinct) {
    EXPECT_TRUE(VoiceGender::Female != VoiceGender::Male);
    EXPECT_TRUE(VoiceGender::Male   != VoiceGender::Unknown);
    EXPECT_TRUE(VoiceGender::Female != VoiceGender::Unknown);
}

// ---------------------------------------------------------------------------
// Voice default construction
// ---------------------------------------------------------------------------
TEST(Voice, DefaultConstructedFieldsAreEmpty) {
    Voice v;
    EXPECT_TRUE(v.name.empty());
    EXPECT_TRUE(v.short_name.empty());
    EXPECT_EQ(v.gender, VoiceGender::Unknown);
    EXPECT_TRUE(v.locale.empty());
    EXPECT_TRUE(v.styles.empty());
}

// ---------------------------------------------------------------------------
// Equality
// ---------------------------------------------------------------------------
TEST(Voice, SameDataIsEqual) {
    Voice a{"Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)",
            "en-US-EmmaMultilingualNeural",
            VoiceGender::Female,
            "en-US",
            {"cheerful", "sad"}};

    Voice b = a;
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(Voice, DifferentNameIsNotEqual) {
    Voice a{"NameA", "sn-A", VoiceGender::Female, "en-US", {}};
    Voice b{"NameB", "sn-A", VoiceGender::Female, "en-US", {}};
    EXPECT_TRUE(a != b);
}

TEST(Voice, DifferentGenderIsNotEqual) {
    Voice a{"N", "sn", VoiceGender::Female, "en-US", {}};
    Voice b{"N", "sn", VoiceGender::Male,   "en-US", {}};
    EXPECT_TRUE(a != b);
}

TEST(Voice, DifferentStylesIsNotEqual) {
    Voice a{"N", "sn", VoiceGender::Unknown, "en-US", {"cheerful"}};
    Voice b{"N", "sn", VoiceGender::Unknown, "en-US", {}};
    EXPECT_TRUE(a != b);
}
