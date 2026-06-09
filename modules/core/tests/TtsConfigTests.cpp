#include "core/TtsConfig.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <string>

using edge_tts::core::BoundaryType;
using edge_tts::core::TtsConfig;
using edge_tts::core::boundary_type_from_string;
using edge_tts::core::to_string;
using edge_tts::core::validate_tts_config;
using edge_tts::common::ErrorCode;

// ---------------------------------------------------------------------------
// TtsConfig::defaults()
// ---------------------------------------------------------------------------

TEST(TtsConfigDefaults, VoiceMatchesReference) {
    const auto cfg = TtsConfig::defaults();
    EXPECT_EQ(cfg.voice, "en-US-EmmaMultilingualNeural");
}

TEST(TtsConfigDefaults, RateMatchesReference) {
    const auto cfg = TtsConfig::defaults();
    EXPECT_EQ(cfg.rate, "+0%");
}

TEST(TtsConfigDefaults, VolumeMatchesReference) {
    const auto cfg = TtsConfig::defaults();
    EXPECT_EQ(cfg.volume, "+0%");
}

TEST(TtsConfigDefaults, PitchMatchesReference) {
    const auto cfg = TtsConfig::defaults();
    EXPECT_EQ(cfg.pitch, "+0Hz");
}

TEST(TtsConfigDefaults, BoundaryTypeMatchesReference) {
    // Python default: "SentenceBoundary"
    const auto cfg = TtsConfig::defaults();
    EXPECT_TRUE(cfg.boundary_type == BoundaryType::sentence);
}

TEST(TtsConfigDefaults, OutputFormatMatchesReference) {
    const auto cfg = TtsConfig::defaults();
    const auto fmt = cfg.output_format;
    EXPECT_EQ(fmt.value(), "audio-24khz-48kbitrate-mono-mp3");
}

TEST(TtsConfigDefaults, ValidatesWithoutError) {
    const auto cfg = TtsConfig::defaults();
    EXPECT_TRUE(validate_tts_config(cfg).has_value());
}

// ---------------------------------------------------------------------------
// validate_tts_config — voice
// ---------------------------------------------------------------------------

TEST(ValidateTtsConfig, ShortVoiceNameAccepted) {
    TtsConfig cfg;
    cfg.voice = "en-US-EmmaMultilingualNeural";
    EXPECT_TRUE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, FullVoiceNameAccepted) {
    TtsConfig cfg;
    cfg.voice =
        "Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)";
    EXPECT_TRUE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, EmptyVoiceFails) {
    TtsConfig cfg;
    cfg.voice = "";
    const auto r = validate_tts_config(cfg);
    EXPECT_FALSE(r.has_value());
    EXPECT_TRUE(r.error().code() == ErrorCode::invalid_argument);
}

TEST(ValidateTtsConfig, EmptyVoiceErrorContainsFieldName) {
    TtsConfig cfg;
    cfg.voice = "";
    const auto r = validate_tts_config(cfg);
    EXPECT_NE(std::string{r.error().message()}.find("voice"), std::string::npos);
}

TEST(ValidateTtsConfig, InvalidVoiceFails) {
    TtsConfig cfg;
    cfg.voice = "not-a-valid-voice";
    EXPECT_FALSE(validate_tts_config(cfg).has_value());
}

// ---------------------------------------------------------------------------
// validate_tts_config — rate
// ---------------------------------------------------------------------------

TEST(ValidateTtsConfig, RateZeroPercent) {
    TtsConfig cfg;
    cfg.rate = "+0%";
    EXPECT_TRUE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, RatePositive50Percent) {
    TtsConfig cfg;
    cfg.rate = "+50%";
    EXPECT_TRUE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, RateNegative50Percent) {
    TtsConfig cfg;
    cfg.rate = "-50%";
    EXPECT_TRUE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, RatePositive100Percent) {
    TtsConfig cfg;
    cfg.rate = "+100%";
    EXPECT_TRUE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, RateMissingSignFails) {
    TtsConfig cfg;
    cfg.rate = "50%";
    const auto r = validate_tts_config(cfg);
    EXPECT_FALSE(r.has_value());
    EXPECT_TRUE(r.error().code() == ErrorCode::invalid_argument);
}

TEST(ValidateTtsConfig, RateWordFails) {
    TtsConfig cfg;
    cfg.rate = "fast";
    EXPECT_FALSE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, RateDoubleSignFails) {
    TtsConfig cfg;
    cfg.rate = "++10%";
    EXPECT_FALSE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, RateNoPercentFails) {
    TtsConfig cfg;
    cfg.rate = "+10";
    EXPECT_FALSE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, RateWordSuffixFails) {
    TtsConfig cfg;
    cfg.rate = "+10percent";
    EXPECT_FALSE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, RateEmptyFails) {
    TtsConfig cfg;
    cfg.rate = "";
    EXPECT_FALSE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, RateErrorContainsFieldName) {
    TtsConfig cfg;
    cfg.rate = "fast";
    const auto r = validate_tts_config(cfg);
    EXPECT_NE(std::string{r.error().message()}.find("rate"), std::string::npos);
}

// ---------------------------------------------------------------------------
// validate_tts_config — volume
// ---------------------------------------------------------------------------

TEST(ValidateTtsConfig, VolumeZeroPercent) {
    TtsConfig cfg;
    cfg.volume = "+0%";
    EXPECT_TRUE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, VolumePositive100Percent) {
    TtsConfig cfg;
    cfg.volume = "+100%";
    EXPECT_TRUE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, VolumeNegative50Percent) {
    TtsConfig cfg;
    cfg.volume = "-50%";
    EXPECT_TRUE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, VolumeWordFails) {
    TtsConfig cfg;
    cfg.volume = "loud";
    EXPECT_FALSE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, VolumeNoSignFails) {
    TtsConfig cfg;
    cfg.volume = "10%";
    EXPECT_FALSE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, VolumeErrorContainsFieldName) {
    TtsConfig cfg;
    cfg.volume = "loud";
    const auto r = validate_tts_config(cfg);
    EXPECT_NE(std::string{r.error().message()}.find("volume"), std::string::npos);
}

// ---------------------------------------------------------------------------
// validate_tts_config — pitch
// ---------------------------------------------------------------------------

TEST(ValidateTtsConfig, PitchZeroHz) {
    TtsConfig cfg;
    cfg.pitch = "+0Hz";
    EXPECT_TRUE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, PitchPositive50Hz) {
    TtsConfig cfg;
    cfg.pitch = "+50Hz";
    EXPECT_TRUE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, PitchNegative50Hz) {
    TtsConfig cfg;
    cfg.pitch = "-50Hz";
    EXPECT_TRUE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, PitchWordFails) {
    TtsConfig cfg;
    cfg.pitch = "high";
    EXPECT_FALSE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, PitchPercent_NotHz_Fails) {
    TtsConfig cfg;
    cfg.pitch = "+10%";
    EXPECT_FALSE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, PitchNoSignFails) {
    TtsConfig cfg;
    cfg.pitch = "10Hz";
    EXPECT_FALSE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, PitchEmptyFails) {
    TtsConfig cfg;
    cfg.pitch = "";
    EXPECT_FALSE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, PitchErrorContainsFieldName) {
    TtsConfig cfg;
    cfg.pitch = "high";
    const auto r = validate_tts_config(cfg);
    EXPECT_NE(std::string{r.error().message()}.find("pitch"), std::string::npos);
}

// ---------------------------------------------------------------------------
// BoundaryType conversion
// ---------------------------------------------------------------------------

TEST(BoundaryType, ToStringWord) {
    EXPECT_EQ(to_string(BoundaryType::word), "WordBoundary");
}

TEST(BoundaryType, ToStringSentence) {
    EXPECT_EQ(to_string(BoundaryType::sentence), "SentenceBoundary");
}

TEST(BoundaryType, FromStringWordBoundary) {
    const auto r = boundary_type_from_string("WordBoundary");
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value() == BoundaryType::word);
}

TEST(BoundaryType, FromStringSentenceBoundary) {
    const auto r = boundary_type_from_string("SentenceBoundary");
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value() == BoundaryType::sentence);
}

TEST(BoundaryType, FromStringUnknownFails) {
    EXPECT_FALSE(boundary_type_from_string("word").has_value());
    EXPECT_FALSE(boundary_type_from_string("sentence").has_value());
    EXPECT_FALSE(boundary_type_from_string("").has_value());
}

TEST(BoundaryType, RoundTrip) {
    const auto rw = boundary_type_from_string(to_string(BoundaryType::word));
    EXPECT_EQ(rw.value(), BoundaryType::word);
    const auto rs = boundary_type_from_string(to_string(BoundaryType::sentence));
    EXPECT_EQ(rs.value(), BoundaryType::sentence);
}

// ---------------------------------------------------------------------------
// OutputFormat integration
// ---------------------------------------------------------------------------

TEST(ValidateTtsConfig, DefaultOutputFormatValid) {
    TtsConfig cfg = TtsConfig::defaults();
    EXPECT_TRUE(validate_tts_config(cfg).has_value());
}

TEST(ValidateTtsConfig, ExplicitOutputFormatField) {
    TtsConfig cfg;
    cfg.output_format = edge_tts::core::OutputFormat::default_format();
    EXPECT_TRUE(validate_tts_config(cfg).has_value());
    const auto fmt = cfg.output_format;
    EXPECT_EQ(fmt.value(), "audio-24khz-48kbitrate-mono-mp3");
}
