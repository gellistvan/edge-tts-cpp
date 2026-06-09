#include "cli/EdgeTtsArguments.hpp"
#include "vendor/minigtest/minigtest.hpp"

using edge_tts::cli::EdgeTtsArguments;

TEST(EdgeTtsArguments, DefaultVoice) {
    EdgeTtsArguments args;
    EXPECT_EQ(args.voice, "en-US-EmmaMultilingualNeural");
}

TEST(EdgeTtsArguments, DefaultRate) {
    EdgeTtsArguments args;
    EXPECT_EQ(args.rate, "+0%");
}

TEST(EdgeTtsArguments, DefaultVolume) {
    EdgeTtsArguments args;
    EXPECT_EQ(args.volume, "+0%");
}

TEST(EdgeTtsArguments, DefaultPitch) {
    EdgeTtsArguments args;
    EXPECT_EQ(args.pitch, "+0Hz");
}

TEST(EdgeTtsArguments, DefaultListVoicesIsFalse) {
    EdgeTtsArguments args;
    EXPECT_FALSE(args.list_voices);
}

TEST(EdgeTtsArguments, OptionalFieldsAreNullByDefault) {
    EdgeTtsArguments args;
    EXPECT_FALSE(args.text.has_value());
    EXPECT_FALSE(args.file.has_value());
    EXPECT_FALSE(args.write_media.has_value());
    EXPECT_FALSE(args.write_subtitles.has_value());
    EXPECT_FALSE(args.proxy.has_value());
}
