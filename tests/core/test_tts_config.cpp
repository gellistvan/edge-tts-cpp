#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/common/Errors.hpp"
#include "vendor/minigtest/minigtest.hpp"

using edge_tts::core::TtsConfig;
using edge_tts::core::BoundaryType;
using edge_tts::core::normalize_voice_name;
using edge_tts::common::ConfigurationError;

// ---------------------------------------------------------------------------
// Default values
// ---------------------------------------------------------------------------
TEST(TtsConfig, DefaultVoice) {
    TtsConfig cfg;
    EXPECT_EQ(cfg.voice, "en-US-EmmaMultilingualNeural");
}

TEST(TtsConfig, DefaultRate) {
    TtsConfig cfg;
    EXPECT_EQ(cfg.rate, "+0%");
}

TEST(TtsConfig, DefaultVolume) {
    TtsConfig cfg;
    EXPECT_EQ(cfg.volume, "+0%");
}

TEST(TtsConfig, DefaultPitch) {
    TtsConfig cfg;
    EXPECT_EQ(cfg.pitch, "+0Hz");
}

TEST(TtsConfig, DefaultBoundary) {
    TtsConfig cfg;
    EXPECT_TRUE(cfg.boundary_type == BoundaryType::sentence);
}

// ---------------------------------------------------------------------------
// Validate: default config passes and normalizes voice
// ---------------------------------------------------------------------------
TEST(TtsConfig, DefaultConfigValidates) {
    TtsConfig cfg;
    EXPECT_NO_THROW(cfg.validate());
}

TEST(TtsConfig, ValidateNormalizesShortVoiceName) {
    TtsConfig cfg;
    cfg.validate();
    EXPECT_EQ(cfg.voice,
        "Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)");
}

TEST(TtsConfig, ValidateIsIdempotent) {
    TtsConfig cfg;
    cfg.validate();
    const std::string after_first = cfg.voice;
    cfg.validate();
    EXPECT_EQ(cfg.voice, after_first);
}

// ---------------------------------------------------------------------------
// Voice normalization
// ---------------------------------------------------------------------------
TEST(TtsConfig, NormalizeSimpleShortName) {
    const auto result = normalize_voice_name("en-US-EmmaMultilingualNeural");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result,
        "Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)");
}

TEST(TtsConfig, NormalizeFilipino) {
    const auto result = normalize_voice_name("fil-PH-AngeloNeural");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result,
        "Microsoft Server Speech Text to Speech Voice (fil-PH, AngeloNeural)");
}

TEST(TtsConfig, NormalizeWelsh) {
    const auto result = normalize_voice_name("cy-GB-NiaNeural");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result,
        "Microsoft Server Speech Text to Speech Voice (cy-GB, NiaNeural)");
}

TEST(TtsConfig, NormalizeCompoundRegion) {
    // "en-US-AndrewMultilingual-CasualNeural"
    //   → region becomes "US-AndrewMultilingual", name becomes "CasualNeural"
    const auto result = normalize_voice_name("en-US-AndrewMultilingual-CasualNeural");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result,
        "Microsoft Server Speech Text to Speech Voice "
        "(en-US-AndrewMultilingual, CasualNeural)");
}

TEST(TtsConfig, NormalizeAlreadyFullForm) {
    const std::string full =
        "Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)";
    const auto result = normalize_voice_name(full);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, full);
}

TEST(TtsConfig, NormalizeGarbageReturnsNullopt) {
    EXPECT_FALSE(normalize_voice_name("not-a-voice").has_value());
    EXPECT_FALSE(normalize_voice_name("").has_value());
    EXPECT_FALSE(normalize_voice_name("EN-US-EmmaMultilingualNeural").has_value()); // upper lang
}

TEST(TtsConfig, ValidateFullFormVoicePasses) {
    TtsConfig cfg;
    cfg.voice =
        "Microsoft Server Speech Text to Speech Voice (en-US, EmmaMultilingualNeural)";
    EXPECT_NO_THROW(cfg.validate());
}

TEST(TtsConfig, ValidateInvalidVoiceThrows) {
    TtsConfig cfg;
    cfg.voice = "invalid-voice";
    EXPECT_THROW(cfg.validate(), ConfigurationError);
}

// ---------------------------------------------------------------------------
// Rate syntax
// ---------------------------------------------------------------------------
TEST(TtsConfig, RatePositivePercentPasses) {
    TtsConfig cfg;
    cfg.rate = "+50%";
    EXPECT_NO_THROW(cfg.validate());
}

TEST(TtsConfig, RateNegativePercentPasses) {
    TtsConfig cfg;
    cfg.rate = "-25%";
    EXPECT_NO_THROW(cfg.validate());
}

TEST(TtsConfig, RateZeroPercentPasses) {
    TtsConfig cfg;
    cfg.rate = "+0%";
    EXPECT_NO_THROW(cfg.validate());
}

TEST(TtsConfig, RateMissingSignThrows) {
    TtsConfig cfg;
    cfg.rate = "50%";
    EXPECT_THROW(cfg.validate(), ConfigurationError);
}

TEST(TtsConfig, RateMissingPercentThrows) {
    TtsConfig cfg;
    cfg.rate = "+50";
    EXPECT_THROW(cfg.validate(), ConfigurationError);
}

TEST(TtsConfig, RateEmptyThrows) {
    TtsConfig cfg;
    cfg.rate = "";
    EXPECT_THROW(cfg.validate(), ConfigurationError);
}

// ---------------------------------------------------------------------------
// Volume syntax
// ---------------------------------------------------------------------------
TEST(TtsConfig, VolumePositivePercentPasses) {
    TtsConfig cfg;
    cfg.volume = "+100%";
    EXPECT_NO_THROW(cfg.validate());
}

TEST(TtsConfig, VolumeNegativePercentPasses) {
    TtsConfig cfg;
    cfg.volume = "-50%";
    EXPECT_NO_THROW(cfg.validate());
}

TEST(TtsConfig, VolumeInvalidThrows) {
    TtsConfig cfg;
    cfg.volume = "100";
    EXPECT_THROW(cfg.validate(), ConfigurationError);
}

// ---------------------------------------------------------------------------
// Pitch syntax
// ---------------------------------------------------------------------------
TEST(TtsConfig, PitchPositiveHzPasses) {
    TtsConfig cfg;
    cfg.pitch = "+100Hz";
    EXPECT_NO_THROW(cfg.validate());
}

TEST(TtsConfig, PitchNegativeHzPasses) {
    TtsConfig cfg;
    cfg.pitch = "-50Hz";
    EXPECT_NO_THROW(cfg.validate());
}

TEST(TtsConfig, PitchZeroHzPasses) {
    TtsConfig cfg;
    cfg.pitch = "+0Hz";
    EXPECT_NO_THROW(cfg.validate());
}

TEST(TtsConfig, PitchMissingHzThrows) {
    TtsConfig cfg;
    cfg.pitch = "+50";
    EXPECT_THROW(cfg.validate(), ConfigurationError);
}

TEST(TtsConfig, PitchPercentInsteadOfHzThrows) {
    TtsConfig cfg;
    cfg.pitch = "+50%";
    EXPECT_THROW(cfg.validate(), ConfigurationError);
}

TEST(TtsConfig, PitchEmptyThrows) {
    TtsConfig cfg;
    cfg.pitch = "";
    EXPECT_THROW(cfg.validate(), ConfigurationError);
}
