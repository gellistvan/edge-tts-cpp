#include "subtitles/SrtComposer.hpp"
#include "subtitles/SubtitleCue.hpp"
#include "subtitles/SubtitleTime.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <cstdint>
#include <string>
#include <vector>

using edge_tts::subtitles::SrtComposer;
using edge_tts::subtitles::SubtitleCue;
using edge_tts::subtitles::SubtitleTime;

static SrtComposer composer{};

// Helper: build a SubtitleTime from milliseconds (bypasses ticks calculation).
// Uses from_edge_ticks(ms * 10000) since 1ms == 10000 ticks.
static SubtitleTime make_time(std::int64_t ms)
{
    return SubtitleTime::from_edge_ticks(ms * 10'000).value();
}

// Helper: build a SubtitleCue inline.
static SubtitleCue make_cue(std::int64_t start_ms, std::int64_t end_ms, const char* text)
{
    return {make_time(start_ms), make_time(end_ms), text};
}

// ---------------------------------------------------------------------------
// Empty cue list
// ---------------------------------------------------------------------------

TEST(SrtComposer, EmptyCuesReturnsEmptyString) {
    const auto r = composer.compose({});
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

// ---------------------------------------------------------------------------
// Single cue
// ---------------------------------------------------------------------------

TEST(SrtComposer, OneCue) {
    const SubtitleCue cues[] = {make_cue(0, 1000, "Hello")};
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(),
        "1\n"
        "00:00:00,000 --> 00:00:01,000\n"
        "Hello\n"
        "\n");
}

// ---------------------------------------------------------------------------
// Two cues
// ---------------------------------------------------------------------------

TEST(SrtComposer, TwoCues) {
    const SubtitleCue cues[] = {
        make_cue(0,    1000, "Hello"),
        make_cue(1500, 2500, "World"),
    };
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(),
        "1\n"
        "00:00:00,000 --> 00:00:01,000\n"
        "Hello\n"
        "\n"
        "2\n"
        "00:00:01,500 --> 00:00:02,500\n"
        "World\n"
        "\n");
}

// ---------------------------------------------------------------------------
// Fixture exact match — basic.srt
//
// Two cues: [500ms→1500ms, "Hello, world."] and [2000ms→3000ms, "Second line."]
// Expected content matches tests/subtitles/fixtures/basic.srt exactly.
// ---------------------------------------------------------------------------

TEST(SrtComposer, FixtureExactMatch) {
    const SubtitleCue cues[] = {
        make_cue( 500, 1500, "Hello, world."),
        make_cue(2000, 3000, "Second line."),
    };
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());

    // This is the exact content of tests/subtitles/fixtures/basic.srt
    const std::string expected =
        "1\n"
        "00:00:00,500 --> 00:00:01,500\n"
        "Hello, world.\n"
        "\n"
        "2\n"
        "00:00:02,000 --> 00:00:03,000\n"
        "Second line.\n"
        "\n";

    EXPECT_EQ(r.value(), expected);
}

// ---------------------------------------------------------------------------
// Text with embedded newline — preserved (make_legal_content keeps single \n)
// ---------------------------------------------------------------------------

TEST(SrtComposer, TextWithSingleNewlinePreserved) {
    const SubtitleCue cues[] = {make_cue(0, 1000, "Line one\nLine two")};
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(),
        "1\n"
        "00:00:00,000 --> 00:00:01,000\n"
        "Line one\nLine two\n"
        "\n");
}

// ---------------------------------------------------------------------------
// Text with consecutive blank lines — collapsed to one newline
// (reference: MULTI_WS_REGEX.sub("\n", ...) collapses \n\n+ → \n)
// ---------------------------------------------------------------------------

TEST(SrtComposer, TextWithDoubleNewlineCollapsed) {
    const SubtitleCue cues[] = {make_cue(0, 1000, "A\n\nB")};
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(),
        "1\n"
        "00:00:00,000 --> 00:00:01,000\n"
        "A\nB\n"  // \n\n collapsed to \n
        "\n");
}

TEST(SrtComposer, TextWithTripleNewlineCollapsed) {
    const SubtitleCue cues[] = {make_cue(0, 1000, "A\n\n\nB")};
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(),
        "1\n"
        "00:00:00,000 --> 00:00:01,000\n"
        "A\nB\n"
        "\n");
}

// ---------------------------------------------------------------------------
// Text with leading/trailing newlines — stripped (reference: content.strip("\n"))
// ---------------------------------------------------------------------------

TEST(SrtComposer, LeadingTrailingNewlinesStripped) {
    const SubtitleCue cues[] = {make_cue(0, 1000, "\nHello\n")};
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(),
        "1\n"
        "00:00:00,000 --> 00:00:01,000\n"
        "Hello\n"
        "\n");
}

// ---------------------------------------------------------------------------
// Text with XML entity — stored verbatim (already unescaped by MetadataJsonParser)
// ---------------------------------------------------------------------------

TEST(SrtComposer, XmlEntityTextStoredVerbatim) {
    // "&" in text (already unescaped from "&amp;" by MetadataJsonParser)
    const SubtitleCue cues[] = {make_cue(0, 1000, "A & B")};
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(),
        "1\n"
        "00:00:00,000 --> 00:00:01,000\n"
        "A & B\n"
        "\n");
}

// ---------------------------------------------------------------------------
// Trailing newline behavior — each block ends with exactly "\n\n"
// ---------------------------------------------------------------------------

TEST(SrtComposer, TrailingDoubleNewline) {
    const SubtitleCue cues[] = {make_cue(0, 1000, "Hi")};
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    const std::string& out = r.value();
    // Must end with "\n\n"
    EXPECT_TRUE(out.size() >= 2);
    EXPECT_EQ(out[out.size() - 1], '\n');
    EXPECT_EQ(out[out.size() - 2], '\n');
}

TEST(SrtComposer, NoCRLF) {
    // Output must use LF only
    const SubtitleCue cues[] = {make_cue(0, 1000, "text")};
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value().find('\r'), std::string::npos);
}

// ---------------------------------------------------------------------------
// start > end — cue is skipped
// ---------------------------------------------------------------------------

TEST(SrtComposer, StartAfterEndSkipped) {
    // start (2000ms) > end (1000ms) → skipped
    const SubtitleCue cues[] = {make_cue(2000, 1000, "Reversed")};
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

TEST(SrtComposer, StartAfterEndSkippedWhileOthersKept) {
    const SubtitleCue cues[] = {
        make_cue(2000, 1000, "Reversed"),  // skipped
        make_cue(0, 1000, "Kept"),          // kept — numbered 1
    };
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    // Only "Kept" appears; since sorting puts it first, it gets index 1
    EXPECT_NE(r.value().find("Kept"), std::string::npos);
    EXPECT_EQ(r.value().find("Reversed"), std::string::npos);
    // Check that the single kept cue is numbered 1
    EXPECT_EQ(r.value().substr(0, 2), "1\n");
}

// ---------------------------------------------------------------------------
// Zero duration — start == end → skipped
// ---------------------------------------------------------------------------

TEST(SrtComposer, ZeroDurationSkipped) {
    const SubtitleCue cues[] = {make_cue(1000, 1000, "Zero duration")};
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

// ---------------------------------------------------------------------------
// Empty / whitespace text — skipped
// ---------------------------------------------------------------------------

TEST(SrtComposer, EmptyTextSkipped) {
    const SubtitleCue cues[] = {make_cue(0, 1000, "")};
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

TEST(SrtComposer, WhitespaceOnlyTextSkipped) {
    const SubtitleCue cues[] = {make_cue(0, 1000, "   \t\n  ")};
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r.value().empty());
}

// ---------------------------------------------------------------------------
// Sorting — cues given out of order are sorted by start time
// ---------------------------------------------------------------------------

TEST(SrtComposer, OutOfOrderCuesSorted) {
    const SubtitleCue cues[] = {
        make_cue(2000, 3000, "Second"),
        make_cue(0,    1000, "First"),
    };
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(),
        "1\n"
        "00:00:00,000 --> 00:00:01,000\n"
        "First\n"
        "\n"
        "2\n"
        "00:00:02,000 --> 00:00:03,000\n"
        "Second\n"
        "\n");
}

// ---------------------------------------------------------------------------
// Index is contiguous after skipped cues
// ---------------------------------------------------------------------------

TEST(SrtComposer, SkippedCueDoesNotConsumeIndex) {
    const SubtitleCue cues[] = {
        make_cue(0,    1000, "One"),
        make_cue(1000, 1000, "Skipped zero-duration"),
        make_cue(2000, 3000, "Two"),
    };
    const auto r = composer.compose(cues);
    EXPECT_TRUE(r.has_value());
    // "One" gets index 1, "Two" gets index 2 (not 3)
    EXPECT_NE(r.value().find("1\n00:00:00,000 --> 00:00:01,000\nOne"), std::string::npos);
    EXPECT_NE(r.value().find("2\n00:00:02,000 --> 00:00:03,000\nTwo"), std::string::npos);
}
