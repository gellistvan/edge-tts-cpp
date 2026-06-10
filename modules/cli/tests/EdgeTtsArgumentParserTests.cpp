#include "cli/EdgeTtsArgumentParser.hpp"
#include "cli/EdgeTtsArguments.hpp"
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

TEST(EdgeTtsArgumentParser, ProxyEmptyStringIsParseError) {
    // Empty proxy URL is obviously invalid — caught at parse time, exit 2.
    auto r = parse({"--text", "hi", "--proxy", ""});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(EdgeTtsArgumentParser, ProxyMissingSchemeIsParseError) {
    // A proxy URL without "://" has no scheme — caught at parse time, exit 2.
    auto r = parse({"--text", "hi", "--proxy", "proxy.example.com:8080"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(EdgeTtsArgumentParser, ProxyBareHostnameIsParseError) {
    auto r = parse({"--text", "hi", "--proxy", "proxyhost"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(EdgeTtsArgumentParser, ProxyWithHttpSchemeIsAccepted) {
    // A well-formed proxy URL passes parse-time validation.
    // Runtime rejection (unsupported) is a separate concern tested in
    // communication and CLI dispatcher tests.
    auto r = parse({"--text", "hi", "--proxy", "http://proxy.example.com:8080"});
    EXPECT_EQ(r.action, ParseAction::synthesize);
    ASSERT_TRUE(r.arguments.proxy.has_value());
    EXPECT_EQ(*r.arguments.proxy, "http://proxy.example.com:8080");
}

TEST(EdgeTtsArgumentParser, ProxyWithHttpsSchemeIsAccepted) {
    auto r = parse({"--text", "hi", "--proxy", "https://proxy.example.com:443"});
    EXPECT_EQ(r.action, ParseAction::synthesize);
    ASSERT_TRUE(r.arguments.proxy.has_value());
}

TEST(EdgeTtsArgumentParser, ProxyParseErrorMessageIsDescriptive) {
    auto r = parse({"--text", "hi", "--proxy", "badvalue"});
    EXPECT_EQ(r.action, ParseAction::error);
    // The message must mention "proxy" and "scheme" so the user knows what to fix.
    EXPECT_TRUE(r.message.find("proxy") != std::string::npos ||
                r.message.find("--proxy") != std::string::npos);
    EXPECT_TRUE(r.message.find("scheme") != std::string::npos ||
                r.message.find("://") != std::string::npos);
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

// ---------------------------------------------------------------------------
// Missing-value errors — every option that requires an argument must produce
// a parse error (exit 2) when the flag is present but no value follows.
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, TextMissingValueIsError) {
    auto r = parse({"--text"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
    EXPECT_NE(r.message.find("--text"), std::string::npos);
}

TEST(EdgeTtsArgumentParser, FileMissingValueIsError) {
    auto r = parse({"--file"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
    EXPECT_NE(r.message.find("--file"), std::string::npos);
}

TEST(EdgeTtsArgumentParser, VoiceMissingValueIsError) {
    auto r = parse({"--text", "hi", "--voice"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
    EXPECT_NE(r.message.find("--voice"), std::string::npos);
}

TEST(EdgeTtsArgumentParser, RateMissingValueIsError) {
    auto r = parse({"--text", "hi", "--rate"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
    EXPECT_NE(r.message.find("--rate"), std::string::npos);
}

TEST(EdgeTtsArgumentParser, VolumeMissingValueIsError) {
    auto r = parse({"--text", "hi", "--volume"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
    EXPECT_NE(r.message.find("--volume"), std::string::npos);
}

TEST(EdgeTtsArgumentParser, PitchMissingValueIsError) {
    auto r = parse({"--text", "hi", "--pitch"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
    EXPECT_NE(r.message.find("--pitch"), std::string::npos);
}

TEST(EdgeTtsArgumentParser, WriteMediaMissingValueIsError) {
    auto r = parse({"--text", "hi", "--write-media"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
    EXPECT_NE(r.message.find("--write-media"), std::string::npos);
}

TEST(EdgeTtsArgumentParser, WriteSubtitlesMissingValueIsError) {
    auto r = parse({"--text", "hi", "--write-subtitles"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
    EXPECT_NE(r.message.find("--write-subtitles"), std::string::npos);
}

TEST(EdgeTtsArgumentParser, ProxyMissingValueIsError) {
    // --proxy with no following token → exit 2.
    auto r = parse({"--text", "hi", "--proxy"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
    EXPECT_NE(r.message.find("--proxy"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Missing-value errors for short forms
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, ShortTextMissingValueIsError) {
    auto r = parse({"-t"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(EdgeTtsArgumentParser, ShortFileMissingValueIsError) {
    auto r = parse({"-f"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(EdgeTtsArgumentParser, ShortVoiceMissingValueIsError) {
    auto r = parse({"--text", "hi", "-v"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

// ---------------------------------------------------------------------------
// Positional argument rejection
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, PositionalArgumentIsError) {
    // Bare words that are not option flags are rejected.
    auto r = parse({"hello"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(EdgeTtsArgumentParser, PositionalAfterOptionsIsError) {
    auto r = parse({"--text", "hi", "extraword"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

// ---------------------------------------------------------------------------
// Negative-value-with-space is a parse error (documented behavior #8)
//
// Reference: CLI_COMPATIBILITY.md behavioral note 8 — `--rate -50%` is
// misinterpreted because `-50%` looks like an option token.  Users must use
// `--rate=-50%` instead.  The C++ parser replicates the Python argparse
// behavior: `--rate -50%` → "requires an argument" error with exit 2.
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, RateNegativeWithSpaceIsError) {
    // "-50%" starts with '-' and has length > 1, so is_option_token() returns
    // true — the parser cannot consume it as the --rate value.
    auto r = parse({"--text", "hi", "--rate", "-50%"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(EdgeTtsArgumentParser, VolumeNegativeWithSpaceIsError) {
    auto r = parse({"--text", "hi", "--volume", "-10%"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(EdgeTtsArgumentParser, PitchNegativeWithSpaceIsError) {
    auto r = parse({"--text", "hi", "--pitch", "-5Hz"});
    EXPECT_EQ(r.action, ParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

// ---------------------------------------------------------------------------
// Help text completeness — every documented CLI option must appear
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, HelpTextContainsAllDocumentedOptions) {
    const std::string h = EdgeTtsArgumentParser{}.help_text();
    // All documented options per CLI_COMPATIBILITY.md option matrix.
    EXPECT_NE(h.find("--text"),           std::string::npos);
    EXPECT_NE(h.find("--file"),           std::string::npos);
    EXPECT_NE(h.find("--list-voices"),    std::string::npos);
    EXPECT_NE(h.find("--voice"),          std::string::npos);
    EXPECT_NE(h.find("--rate"),           std::string::npos);
    EXPECT_NE(h.find("--volume"),         std::string::npos);
    EXPECT_NE(h.find("--pitch"),          std::string::npos);
    EXPECT_NE(h.find("--write-media"),    std::string::npos);
    EXPECT_NE(h.find("--write-subtitles"),std::string::npos);
    EXPECT_NE(h.find("--proxy"),          std::string::npos);
    EXPECT_NE(h.find("--version"),        std::string::npos);
    EXPECT_NE(h.find("--help"),           std::string::npos);
}

TEST(EdgeTtsArgumentParser, HelpTextMentionsNegativeValueSyntax) {
    // CLI_COMPATIBILITY.md behavioral note 8: help must document that negative
    // values need the = form (e.g. --rate=-50%).
    const std::string h = EdgeTtsArgumentParser{}.help_text();
    EXPECT_NE(h.find("="),                std::string::npos);
    EXPECT_NE(h.find("negative"),         std::string::npos);
}

// ---------------------------------------------------------------------------
// Version string format
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, VersionStringContainsVersionNumber) {
    // Must contain a version number of the form major.minor.patch.
    const std::string v = EdgeTtsArgumentParser::version_string();
    // At minimum, there must be two dots in the string (x.y.z).
    const auto first_dot  = v.find('.');
    EXPECT_NE(first_dot, std::string::npos);
    const auto second_dot = v.find('.', first_dot + 1);
    EXPECT_NE(second_dot, std::string::npos);
}

// ---------------------------------------------------------------------------
// Empty-string text is a valid parse (not a parse error).
// The synthesizer accepts empty text and returns no audio — that is a runtime
// outcome, not an argument error.  The parser must not reject it.
// ---------------------------------------------------------------------------

TEST(EdgeTtsArgumentParser, EmptyStringTextIsAccepted) {
    auto r = parse({"--text", ""});
    EXPECT_EQ(r.action, ParseAction::synthesize);
    EXPECT_TRUE(r.arguments.text.has_value());
    EXPECT_EQ(*r.arguments.text, "");
}

// --file "" is also a valid parse: the path is stored as-is.
// The open will fail at dispatch time (io_error), not parse time (exit 2).
TEST(EdgeTtsArgumentParser, EmptyStringFileIsAcceptedAtParseTime) {
    auto r = parse({"--file", ""});
    EXPECT_EQ(r.action, ParseAction::synthesize);
    EXPECT_TRUE(r.arguments.file.has_value());
    EXPECT_EQ(*r.arguments.file, "");
}
