#include "core/Voice.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <string>

using edge_tts::core::Voice;
using edge_tts::core::VoiceGender;
using edge_tts::core::voice_gender_from_string;
using edge_tts::core::to_string;
using edge_tts::common::ErrorCode;

// ---------------------------------------------------------------------------
// Gender string conversion
// ---------------------------------------------------------------------------

TEST(VoiceGenderConversion, FemaleFromString) {
    const auto r = voice_gender_from_string("Female");
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value() == VoiceGender::female);
}

TEST(VoiceGenderConversion, MaleFromString) {
    const auto r = voice_gender_from_string("Male");
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value() == VoiceGender::male);
}

TEST(VoiceGenderConversion, UnknownStringFails) {
    EXPECT_FALSE(voice_gender_from_string("unknown").has_value());
    EXPECT_FALSE(voice_gender_from_string("FEMALE").has_value());
    EXPECT_FALSE(voice_gender_from_string("female").has_value());
    EXPECT_FALSE(voice_gender_from_string("").has_value());
}

TEST(VoiceGenderConversion, UnknownValueErrorCode) {
    const auto r = voice_gender_from_string("NotAGender");
    EXPECT_TRUE(r.error().code() == ErrorCode::invalid_argument);
}

TEST(VoiceGenderConversion, UnknownValueCarriesContext) {
    const auto r = voice_gender_from_string("NotAGender");
    EXPECT_EQ(r.error().context(), "NotAGender");
}

TEST(VoiceGenderConversion, ToStringFemale) {
    EXPECT_EQ(to_string(VoiceGender::female), "Female");
}

TEST(VoiceGenderConversion, ToStringMale) {
    EXPECT_EQ(to_string(VoiceGender::male), "Male");
}

TEST(VoiceGenderConversion, ToStringUnknown) {
    EXPECT_EQ(to_string(VoiceGender::unknown), "Unknown");
}

TEST(VoiceGenderConversion, RoundTripFemale) {
    const auto r = voice_gender_from_string(to_string(VoiceGender::female));
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value() == VoiceGender::female);
}

TEST(VoiceGenderConversion, RoundTripMale) {
    const auto r = voice_gender_from_string(to_string(VoiceGender::male));
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value() == VoiceGender::male);
}

// ---------------------------------------------------------------------------
// Voice construction and defaults
// ---------------------------------------------------------------------------

TEST(VoiceConstruct, AllFieldsDefaultToEmpty) {
    Voice v;
    EXPECT_TRUE(v.name.empty());
    EXPECT_TRUE(v.short_name.empty());
    EXPECT_TRUE(v.gender == VoiceGender::unknown);
    EXPECT_TRUE(v.locale.empty());
    EXPECT_TRUE(v.friendly_name.empty());
    EXPECT_TRUE(v.status.empty());
    EXPECT_TRUE(v.suggested_codec.empty());
    EXPECT_TRUE(v.content_categories.empty());
    EXPECT_TRUE(v.voice_personalities.empty());
    EXPECT_TRUE(v.language.empty());
}

TEST(VoiceConstruct, AllFieldsCanBeSet) {
    Voice v;
    v.name               = "Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)";
    v.short_name         = "en-US-EmmaMultilingualNeural";
    v.gender             = VoiceGender::female;
    v.locale             = "en-US";
    v.friendly_name      = "Microsoft EmmaMultilingual Online (Natural) - English (United States)";
    v.status             = "GA";
    v.suggested_codec    = "audio-24khz-48kbitrate-mono-mp3";
    v.content_categories = {"General"};
    v.voice_personalities= {"Friendly", "Positive"};
    v.language           = "en";

    EXPECT_EQ(v.name, "Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)");
    EXPECT_EQ(v.short_name, "en-US-EmmaMultilingualNeural");
    EXPECT_TRUE(v.gender == VoiceGender::female);
    EXPECT_EQ(v.locale, "en-US");
    EXPECT_EQ(v.friendly_name, "Microsoft EmmaMultilingual Online (Natural) - English (United States)");
    EXPECT_EQ(v.status, "GA");
    EXPECT_EQ(v.suggested_codec, "audio-24khz-48kbitrate-mono-mp3");
    EXPECT_EQ(v.content_categories.size(), 1u);
    EXPECT_EQ(v.content_categories[0], "General");
    EXPECT_EQ(v.voice_personalities.size(), 2u);
    EXPECT_EQ(v.language, "en");
}

TEST(VoiceConstruct, StatusValues) {
    Voice v;
    v.status = "GA";      EXPECT_EQ(v.status, "GA");
    v.status = "Preview"; EXPECT_EQ(v.status, "Preview");
    v.status = "Deprecated"; EXPECT_EQ(v.status, "Deprecated");
}

// ---------------------------------------------------------------------------
// Equality
// ---------------------------------------------------------------------------

TEST(VoiceEquality, EmptyVoicesAreEqual) {
    Voice a, b;
    EXPECT_TRUE(a == b);
}

TEST(VoiceEquality, CopiedVoiceIsEqual) {
    Voice a;
    a.name = "N"; a.short_name = "sn"; a.gender = VoiceGender::male;
    a.locale = "de-DE"; a.language = "de"; a.status = "GA";
    const Voice b = a;
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(VoiceEquality, DifferentStatusNotEqual) {
    Voice a, b;
    a.name = b.name = "N";
    a.status = "GA"; b.status = "Preview";
    EXPECT_TRUE(a != b);
}

TEST(VoiceEquality, DifferentFriendlyNameNotEqual) {
    Voice a, b;
    a.friendly_name = "X"; b.friendly_name = "Y";
    EXPECT_TRUE(a != b);
}

TEST(VoiceEquality, DifferentLanguageNotEqual) {
    Voice a, b;
    a.language = "en"; b.language = "de";
    EXPECT_TRUE(a != b);
}

// ---------------------------------------------------------------------------
// to_string output format
// ---------------------------------------------------------------------------

TEST(VoiceGenderToString, IsExactWireValue) {
    // The wire values must match the Python TypedDict Literal exactly.
    const auto f = to_string(VoiceGender::female);
    const auto m = to_string(VoiceGender::male);
    EXPECT_EQ(f, "Female");
    EXPECT_EQ(m, "Male");
    // First letter uppercase, rest lowercase — matches Python's "Female"/"Male"
    EXPECT_EQ(f[0], 'F');
    EXPECT_EQ(m[0], 'M');
}
