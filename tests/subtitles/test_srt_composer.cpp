#include "edge_tts/subtitles/SrtComposer.hpp"

#ifndef EDGE_TTS_NO_GTEST
#include <gtest/gtest.h>

TEST(SrtComposer, ComposesSingleEntry) {
    std::vector<edge_tts::subtitles::SubtitleEntry> entries{{
        std::chrono::milliseconds{0},
        std::chrono::milliseconds{1500},
        "Hello"
    }};
    const auto srt = edge_tts::subtitles::SrtComposer::compose(entries);
    EXPECT_NE(srt.find("00:00:00,000 --> 00:00:01,500"), std::string::npos);
    EXPECT_NE(srt.find("Hello"), std::string::npos);
}
#endif
