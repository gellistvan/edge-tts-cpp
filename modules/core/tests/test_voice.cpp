#include "core/Voice.hpp"
#include "vendor/minigtest/minigtest.hpp"

using edge_tts::core::Voice;
using edge_tts::core::VoiceGender;

TEST(VoiceGender, ValuesAreDistinct) {
    EXPECT_TRUE(VoiceGender::female  != VoiceGender::male);
    EXPECT_TRUE(VoiceGender::male    != VoiceGender::unknown);
    EXPECT_TRUE(VoiceGender::female  != VoiceGender::unknown);
}

TEST(Voice, DefaultConstructedFieldsAreEmpty) {
    Voice v;
    EXPECT_TRUE(v.name.empty());
    EXPECT_TRUE(v.short_name.empty());
    EXPECT_EQ(v.gender, VoiceGender::unknown);
    EXPECT_TRUE(v.locale.empty());
    EXPECT_TRUE(v.content_categories.empty());
    EXPECT_TRUE(v.voice_personalities.empty());
}

TEST(Voice, SameDataIsEqual) {
    Voice a;
    a.name       = "Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)";
    a.short_name = "en-US-EmmaMultilingualNeural";
    a.gender     = VoiceGender::female;
    a.locale     = "en-US";
    a.language   = "en";
    Voice b = a;
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(Voice, DifferentNameIsNotEqual) {
    Voice a, b;
    a.name = "NameA"; b.name = "NameB";
    EXPECT_TRUE(a != b);
}

TEST(Voice, DifferentGenderIsNotEqual) {
    Voice a, b;
    a.gender = VoiceGender::female; b.gender = VoiceGender::male;
    EXPECT_TRUE(a != b);
}

TEST(Voice, DifferentContentCategoriesIsNotEqual) {
    Voice a, b;
    a.content_categories = {"General"}; b.content_categories = {};
    EXPECT_TRUE(a != b);
}
