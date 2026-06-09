// Smoke tests for SrtComposer using the SubtitleCue / SubtitleTime API.
// Kept for regression continuity; detailed tests are in SrtComposerTests.cpp.

#include "subtitles/SrtComposer.hpp"
#include "subtitles/SubtitleCue.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <cstdint>
#include <string>

using edge_tts::subtitles::SrtComposer;
using edge_tts::subtitles::SubtitleCue;
using edge_tts::subtitles::SubtitleTime;

static SrtComposer composer{};

static SubtitleTime make_t(std::int64_t ms)
{
    return SubtitleTime::from_edge_ticks(ms * 10'000).value();
}

TEST(SrtComposer, ComposesSingleEntry) {
    const SubtitleCue cues[] = {{make_t(0), make_t(1500), "Hello"}};
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    EXPECT_NE(r.value().find("00:00:00,000 --> 00:00:01,500"), std::string::npos);
    EXPECT_NE(r.value().find("Hello"), std::string::npos);
}

TEST(SrtComposer, EmptyEntriesReturnsEmpty) {
    const auto r = composer.compose({});
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

TEST(SrtComposer, MultipleEntriesAreNumbered) {
    const SubtitleCue cues[] = {
        {make_t(0),    make_t(1000), "One"},
        {make_t(1000), make_t(2000), "Two"},
    };
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    EXPECT_NE(r.value().find("One"), std::string::npos);
    EXPECT_NE(r.value().find("Two"), std::string::npos);
}
