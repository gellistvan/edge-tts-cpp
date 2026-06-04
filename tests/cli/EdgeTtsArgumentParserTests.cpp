#include "edge_tts/cli/EdgeTtsArgumentParser.hpp"
#include "edge_tts/cli/EdgeTtsArguments.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <string>
#include <vector>

using edge_tts::cli::EdgeTtsArgumentParser;
using edge_tts::cli::EdgeTtsArguments;
using edge_tts::cli::ParseAction;
using edge_tts::cli::ParseResult;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static ParseResult parse(std::vector<std::string> args) {
    return EdgeTtsArgumentParser{}.parse(args);
}

// ---------------------------------------------------------------------------
// No arguments
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, NoArgsIsError) {
    auto r = parse({});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
    EXPECT_FALSE(r.message.empty());
}

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, LongHelpReturnsHelp) {
    auto r = parse({"--help"});
    EXPECT_EQ(r.action, ParseAction::help);
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_FALSE(r.message.empty());
}

TEST(EdgeTtsArgumentParser, ShortHelpReturnsHelp) {
    auto r = parse({"-h"});
    EXPECT_EQ(r.action, ParseAction::help);
    EXPECT_EQ(r.exit_code, 0);
}

TEST(EdgeTtsArgumentParser, HelpTextContainsOptions) {
    auto r = parse({"--help"});
    EXPECT_NE(r.message.find("--text"), std::string::npos);
    EXPECT_NE(r.message.find("--voice"), std::string::npos);
    EXPECT_NE(r.message.find("--list-voices"), std::string::npos);
}

// Help short-circuits even when other options are present / invalid.
TEST(EdgeTtsArgumentParser, HelpShortCircuitsInvalidArgs) {
    auto r = parse({"--help", "--unknown-flag"});
    EXPECT_EQ(r.action, ParseAction::help);
}

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, VersionReturnsVersion) {
    auto r = parse({"--version"});
    EXPECT_EQ(r.action, ParseAction::version);
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_FALSE(r.message.empty());
}

TEST(EdgeTtsArgumentParser, VersionStringContainsProjectName) {
    // C++ deviation: "edge-tts-cpp" not "edge-tts"; see CLI_COMPATIBILITY.md.
    const std::string v = EdgeTtsArgumentParser::version_string();
    EXPECT_NE(v.find("edge-tts-cpp"), std::string::npos);
}

// ---------------------------------------------------------------------------
// --text / -t
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, TextLongForm) {
    auto r = parse({"--text", "hello world"});
    EXPECT_EQ(r.action, ParseAction::synthesize);
    EXPECT_TRUE(r.arguments.text.has_value());
    EXPECT_EQ(*r.arguments.text, "hello world");
}

TEST(EdgeTtsArgumentParser, TextShortForm) {
    auto r = parse({"-t", "hello"});
    EXPECT_EQ(r.action, ParseAction::synthesize);
    EXPECT_TRUE(r.arguments.text.has_value());
    EXPECT_EQ(*r.arguments.text, "hello");
}

TEST(EdgeTtsArgumentParser, TextEqualsForm) {
    auto r = parse({"--text=hello world"});
    EXPECT_EQ(r.action, ParseAction::synthesize);
    EXPECT_EQ(*r.arguments.text, "hello world");
}

// ---------------------------------------------------------------------------
// --file / -f
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, FileLongForm) {
    auto r = parse({"--file", "input.txt"});
    EXPECT_EQ(r.action, ParseAction::synthesize);
    EXPECT_TRUE(r.arguments.file.has_value());
    EXPECT_EQ(*r.arguments.file, "input.txt");
}

TEST(EdgeTtsArgumentParser, FileShortForm) {
    auto r = parse({"-f", "input.txt"});
    EXPECT_EQ(r.action, ParseAction::synthesize);
    EXPECT_EQ(*r.arguments.file, "input.txt");
}

TEST(EdgeTtsArgumentParser, FileStdinDash) {
    // Reference: "-" is treated as stdin by the caller; parser stores as-is.
    auto r = parse({"--file", "-"});
    EXPECT_EQ(r.action, ParseAction::synthesize);
    EXPECT_EQ(*r.arguments.file, "-");
}

TEST(EdgeTtsArgumentParser, FileDevStdin) {
    auto r = parse({"--file", "/dev/stdin"});
    EXPECT_EQ(r.action, ParseAction::synthesize);
    EXPECT_EQ(*r.arguments.file, "/dev/stdin");
}

// ---------------------------------------------------------------------------
// --text/--file/--list-voices mutual exclusion
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, TextAndFileConflict) {
    auto r = parse({"--text", "hello", "--file", "f.txt"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(EdgeTtsArgumentParser, TextAndListVoicesConflict) {
    auto r = parse({"--text", "hello", "--list-voices"});
    EXPECT_EQ(r.action, ParseAction::error);
}

TEST(EdgeTtsArgumentParser, FileAndListVoicesConflict) {
    auto r = parse({"--file", "f.txt", "--list-voices"});
    EXPECT_EQ(r.action, ParseAction::error);
}

// ---------------------------------------------------------------------------
// --list-voices / -l
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, ListVoicesLong) {
    auto r = parse({"--list-voices"});
    EXPECT_EQ(r.action, ParseAction::list_voices);
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.arguments.list_voices);
}

TEST(EdgeTtsArgumentParser, ListVoicesShort) {
    auto r = parse({"-l"});
    EXPECT_EQ(r.action, ParseAction::list_voices);
    EXPECT_TRUE(r.arguments.list_voices);
}

TEST(EdgeTtsArgumentParser, ListVoicesWithProxy) {
    auto r = parse({"--list-voices", "--proxy", "http://proxy:3128"});
    EXPECT_EQ(r.action, ParseAction::list_voices);
    EXPECT_TRUE(r.arguments.proxy.has_value());
    EXPECT_EQ(*r.arguments.proxy, "http://proxy:3128");
}

// ---------------------------------------------------------------------------
// --voice / -v
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, VoiceLongForm) {
    auto r = parse({"--text", "hi", "--voice", "en-GB-RyanNeural"});
    EXPECT_EQ(r.action, ParseAction::synthesize);
    EXPECT_EQ(r.arguments.voice, "en-GB-RyanNeural");
}

TEST(EdgeTtsArgumentParser, VoiceShortForm) {
    auto r = parse({"-t", "hi", "-v", "en-GB-RyanNeural"});
    EXPECT_EQ(r.arguments.voice, "en-GB-RyanNeural");
}

TEST(EdgeTtsArgumentParser, VoiceEqualsForm) {
    auto r = parse({"--text", "hi", "--voice=ar-EG-SalmaNeural"});
    EXPECT_EQ(r.arguments.voice, "ar-EG-SalmaNeural");
}

// ---------------------------------------------------------------------------
// --rate
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, Rate) {
    auto r = parse({"--text", "hi", "--rate", "+50%"});
    EXPECT_EQ(r.arguments.rate, "+50%");
}

TEST(EdgeTtsArgumentParser, RateEqualsNegative) {
    // Reference: negative values require --rate=-50% not --rate -50%.
    auto r = parse({"--text", "hi", "--rate=-50%"});
    EXPECT_EQ(r.action, ParseAction::synthesize);
    EXPECT_EQ(r.arguments.rate, "-50%");
}

// ---------------------------------------------------------------------------
// --volume
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, Volume) {
    auto r = parse({"--text", "hi", "--volume", "+20%"});
    EXPECT_EQ(r.arguments.volume, "+20%");
}

TEST(EdgeTtsArgumentParser, VolumeEqualsNegative) {
    auto r = parse({"--text", "hi", "--volume=-10%"});
    EXPECT_EQ(r.action, ParseAction::synthesize);
    EXPECT_EQ(r.arguments.volume, "-10%");
}

// ---------------------------------------------------------------------------
// --pitch
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, Pitch) {
    auto r = parse({"--text", "hi", "--pitch", "+10Hz"});
    EXPECT_EQ(r.arguments.pitch, "+10Hz");
}

TEST(EdgeTtsArgumentParser, PitchEqualsNegative) {
    auto r = parse({"--text", "hi", "--pitch=-10Hz"});
    EXPECT_EQ(r.action, ParseAction::synthesize);
    EXPECT_EQ(r.arguments.pitch, "-10Hz");
}

// ---------------------------------------------------------------------------
// --write-media
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, WriteMedia) {
    auto r = parse({"--text", "hi", "--write-media", "hello.mp3"});
    EXPECT_TRUE(r.arguments.write_media.has_value());
    EXPECT_EQ(*r.arguments.write_media, "hello.mp3");
}

TEST(EdgeTtsArgumentParser, WriteMediaDashIsStdout) {
    auto r = parse({"--text", "hi", "--write-media", "-"});
    EXPECT_EQ(*r.arguments.write_media, "-");
}

TEST(EdgeTtsArgumentParser, WriteMediaEqualsForm) {
    auto r = parse({"--text", "hi", "--write-media=out.mp3"});
    EXPECT_EQ(*r.arguments.write_media, "out.mp3");
}

// ---------------------------------------------------------------------------
// --write-subtitles
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, WriteSubtitles) {
    auto r = parse({"--text", "hi", "--write-subtitles", "hello.srt"});
    EXPECT_TRUE(r.arguments.write_subtitles.has_value());
    EXPECT_EQ(*r.arguments.write_subtitles, "hello.srt");
}

TEST(EdgeTtsArgumentParser, WriteSubtitlesDashIsStderr) {
    // Reference: "-" → subtitles go to stderr (handled by caller).
    auto r = parse({"--text", "hi", "--write-subtitles", "-"});
    EXPECT_EQ(*r.arguments.write_subtitles, "-");
}

// ---------------------------------------------------------------------------
// --proxy
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, Proxy) {
    auto r = parse({"--text", "hi", "--proxy", "http://proxy.example.com:8080"});
    EXPECT_TRUE(r.arguments.proxy.has_value());
    EXPECT_EQ(*r.arguments.proxy, "http://proxy.example.com:8080");
}

TEST(EdgeTtsArgumentParser, ProxyEqualsForm) {
    auto r = parse({"--text", "hi", "--proxy=http://p:3128"});
    EXPECT_EQ(*r.arguments.proxy, "http://p:3128");
}

// ---------------------------------------------------------------------------
// Unknown / unsupported options
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, UnknownLongOptionIsError) {
    auto r = parse({"--text", "hi", "--unknown-flag"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(EdgeTtsArgumentParser, UnknownShortOptionIsError) {
    auto r = parse({"--text", "hi", "-z"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(EdgeTtsArgumentParser, FormatOptionIsNotSupported) {
    // Reference: --format is NOT a CLI option (hardcoded format, no flag).
    // See CLI_COMPATIBILITY.md behavioral note 5.
    auto r = parse({"--text", "hi", "--format", "mp3"});
    EXPECT_EQ(r.action, ParseAction::error);
}

// ---------------------------------------------------------------------------
// Default values — must match Python reference
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, DefaultVoiceMatchesReference) {
    auto r = parse({"--text", "hi"});
    EXPECT_EQ(r.arguments.voice, "en-US-EmmaMultilingualNeural");
}

TEST(EdgeTtsArgumentParser, DefaultRateMatchesReference) {
    auto r = parse({"--text", "hi"});
    EXPECT_EQ(r.arguments.rate, "+0%");
}

TEST(EdgeTtsArgumentParser, DefaultVolumeMatchesReference) {
    auto r = parse({"--text", "hi"});
    EXPECT_EQ(r.arguments.volume, "+0%");
}

TEST(EdgeTtsArgumentParser, DefaultPitchMatchesReference) {
    auto r = parse({"--text", "hi"});
    EXPECT_EQ(r.arguments.pitch, "+0Hz");
}

TEST(EdgeTtsArgumentParser, DefaultWriteMediaIsAbsent) {
    auto r = parse({"--text", "hi"});
    EXPECT_FALSE(r.arguments.write_media.has_value());
}

TEST(EdgeTtsArgumentParser, DefaultWriteSubtitlesIsAbsent) {
    auto r = parse({"--text", "hi"});
    EXPECT_FALSE(r.arguments.write_subtitles.has_value());
}

TEST(EdgeTtsArgumentParser, DefaultProxyIsAbsent) {
    auto r = parse({"--text", "hi"});
    EXPECT_FALSE(r.arguments.proxy.has_value());
}

TEST(EdgeTtsArgumentParser, DefaultListVoicesIsFalse) {
    auto r = parse({"--text", "hi"});
    EXPECT_FALSE(r.arguments.list_voices);
}

// ---------------------------------------------------------------------------
// argc/argv interface
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, ArgcArgvSkipsArgv0) {
    const char* argv[] = {"edge-tts", "--text", "hello", nullptr};
    auto r = EdgeTtsArgumentParser{}.parse(3, argv);
    EXPECT_EQ(r.action, ParseAction::synthesize);
    EXPECT_EQ(*r.arguments.text, "hello");
}
