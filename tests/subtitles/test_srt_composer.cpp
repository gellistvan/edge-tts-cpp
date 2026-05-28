#include "edge_tts/subtitles/SrtComposer.hpp"
#include "vendor/minigtest/minigtest.hpp"

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

TEST(SrtComposer, EmptyEntriesReturnsEmpty) {
    const auto srt = edge_tts::subtitles::SrtComposer::compose({});
    EXPECT_TRUE(srt.empty());
}

TEST(SrtComposer, MultipleEntriesAreNumbered) {
    std::vector<edge_tts::subtitles::SubtitleEntry> entries{
        {std::chrono::milliseconds{0},    std::chrono::milliseconds{1000}, "One"},
        {std::chrono::milliseconds{1000}, std::chrono::milliseconds{2000}, "Two"},
    };
    const auto srt = edge_tts::subtitles::SrtComposer::compose(entries);
    EXPECT_NE(srt.find("One"), std::string::npos);
    EXPECT_NE(srt.find("Two"), std::string::npos);
}
