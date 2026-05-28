#include "edge_tts/cli/CliOptions.hpp"
#include "vendor/minigtest/minigtest.hpp"

using edge_tts::cli::CliOptions;

TEST(CliOptions, DefaultVoice) {
    CliOptions opts;
    EXPECT_EQ(opts.voice, "en-US-EmmaMultilingualNeural");
}

TEST(CliOptions, DefaultRate) {
    CliOptions opts;
    EXPECT_EQ(opts.rate, "+0%");
}

TEST(CliOptions, DefaultVolume) {
    CliOptions opts;
    EXPECT_EQ(opts.volume, "+0%");
}

TEST(CliOptions, DefaultPitch) {
    CliOptions opts;
    EXPECT_EQ(opts.pitch, "+0Hz");
}

TEST(CliOptions, DefaultListVoicesIsFalse) {
    CliOptions opts;
    EXPECT_FALSE(opts.list_voices);
}

TEST(CliOptions, OptionalFieldsAreNullByDefault) {
    CliOptions opts;
    EXPECT_FALSE(opts.file.has_value());
    EXPECT_FALSE(opts.write_media.has_value());
    EXPECT_FALSE(opts.write_subtitles.has_value());
    EXPECT_FALSE(opts.proxy.has_value());
}
