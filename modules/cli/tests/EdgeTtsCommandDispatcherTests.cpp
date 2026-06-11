#include "cli/EdgeTtsCommandDispatcher.hpp"
#include "cli/EdgeTtsArgumentParser.hpp"
#include "api/SpeechSynthesizer.hpp"
#include "api/SynthesisOptions.hpp"
#include "common/Error.hpp"
#include "core/Chunk.hpp"
#include "core/TtsConfig.hpp"
#include "core/Voice.hpp"
#include "support/ChunkTestHelpers.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using edge_tts::api::SpeechSynthesizer;
using edge_tts::api::SynthesisOptions;
using edge_tts::api::SynthesizerFn;
using edge_tts::cli::EdgeTtsArgumentParser;
using edge_tts::cli::EdgeTtsArguments;
using edge_tts::cli::EdgeTtsCommandDispatcher;
using edge_tts::cli::ParseAction;
using edge_tts::cli::ParseResult;
using edge_tts::common::Error;
using edge_tts::common::ErrorCode;
using edge_tts::core::AudioChunk;
using edge_tts::core::BoundaryChunk;
using edge_tts::core::BoundaryEventType;
using edge_tts::core::TtsChunk;
using edge_tts::core::TtsConfig;
using edge_tts::core::Voice;
using edge_tts::core::VoiceGender;
using edge_tts::test::make_audio;
using edge_tts::test::make_boundary;

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static fs::path tmp_path(const std::string& name) {
    return fs::temp_directory_path() / ("edge_tts_cmd_test_" + name);
}

struct FileGuard {
    fs::path path;
    ~FileGuard() { fs::remove(path); }
};

static std::string read_text_file(const fs::path& p) {
    std::ifstream f(p);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>{}};
}

static std::vector<std::byte> read_binary_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    const std::vector<char> buf{std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>{}};
    std::vector<std::byte> out(buf.size());
    for (std::size_t i = 0; i < buf.size(); ++i)
        out[i] = static_cast<std::byte>(buf[i]);
    return out;
}

// Build a Voice struct.
static Voice make_voice(std::string short_name,
                        VoiceGender gender,
                        std::vector<std::string> cats  = {"General"},
                        std::vector<std::string> perss = {"Friendly"}) {
    Voice v;
    v.short_name          = std::move(short_name);
    v.gender              = gender;
    v.content_categories  = std::move(cats);
    v.voice_personalities = std::move(perss);
    return v;
}

// Factory that creates a SpeechSynthesizer with a fixed response.
static EdgeTtsCommandDispatcher::SynthesizerFactory
make_factory(std::vector<TtsChunk> chunks) {
    return [chunks = std::move(chunks)](
               std::string text, TtsConfig cfg, SynthesisOptions opts) {
        return SpeechSynthesizer(std::move(text), std::move(cfg), std::move(opts),
            [chunks](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok(chunks);
            });
    };
}

// Factory that injects a synthesis error.
static EdgeTtsCommandDispatcher::SynthesizerFactory
make_failing_factory(ErrorCode code, std::string msg) {
    return [code, msg = std::move(msg)](
               std::string text, TtsConfig cfg, SynthesisOptions opts) {
        return SpeechSynthesizer(std::move(text), std::move(cfg), std::move(opts),
            [code, msg](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::fail(
                    Error{code, msg});
            });
    };
}

// Voice service that succeeds.
static EdgeTtsCommandDispatcher::VoiceServiceFn
make_voice_svc(std::vector<Voice> vs) {
    return [vs = std::move(vs)]() mutable {
        return edge_tts::common::Result<std::vector<Voice>>::ok(vs);
    };
}

// Voice service that fails.
static EdgeTtsCommandDispatcher::VoiceServiceFn
make_failing_voice_svc(ErrorCode code, std::string msg) {
    return [code, msg = std::move(msg)]() {
        return edge_tts::common::Result<std::vector<Voice>>::fail(Error{code, msg});
    };
}

// Build a ParseResult for synthesize with a text argument.
static ParseResult make_text_result(std::string text,
                                    std::optional<std::string> write_media    = {},
                                    std::optional<std::string> write_subtitles = {}) {
    ParseResult r;
    r.action      = ParseAction::synthesize;
    r.exit_code   = 0;
    r.arguments   = EdgeTtsArguments{};
    r.arguments.text             = std::move(text);
    r.arguments.write_media      = std::move(write_media);
    r.arguments.write_subtitles  = std::move(write_subtitles);
    return r;
}

// ---------------------------------------------------------------------------
// list-voices: calls voice service
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, ListVoicesCallsVoiceService) {
    bool called = false;
    auto svc = [&called]() {
        called = true;
        return edge_tts::common::Result<std::vector<Voice>>::ok({});
    };

    std::ostringstream out, err;
    std::istringstream in;
    ParseResult r;
    r.action = ParseAction::list_voices;
    r.exit_code = 0;

    EdgeTtsCommandDispatcher d{svc, make_factory({}), out, err, in};
    d.dispatch(r);
    EXPECT_TRUE(called);
}

TEST(EdgeTtsCommandDispatcher, ListVoicesExitCodeSuccess) {
    std::ostringstream out, err;
    std::istringstream in;
    ParseResult r;
    r.action = ParseAction::list_voices;
    r.exit_code = 0;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory({}), out, err, in};
    EXPECT_EQ(d.dispatch(r), 0);
}

// ---------------------------------------------------------------------------
// list-voices: formatted output to stdout
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, ListVoicesPrintsFormattedTable) {
    std::vector<Voice> vs = {
        make_voice("en-US-EmmaMultilingualNeural", VoiceGender::female),
        make_voice("af-ZA-AdriNeural",             VoiceGender::female),
    };

    std::ostringstream out, err;
    std::istringstream in;
    ParseResult r;
    r.action = ParseAction::list_voices;
    r.exit_code = 0;

    EdgeTtsCommandDispatcher d{make_voice_svc(vs), make_factory({}), out, err, in};
    d.dispatch(r);

    const std::string output = out.str();
    // Must contain headers and both voice names.
    EXPECT_NE(output.find("Name"),    std::string::npos);
    EXPECT_NE(output.find("Gender"),  std::string::npos);
    EXPECT_NE(output.find("af-ZA-AdriNeural"),             std::string::npos);
    EXPECT_NE(output.find("en-US-EmmaMultilingualNeural"), std::string::npos);
}

TEST(EdgeTtsCommandDispatcher, ListVoicesSortedInOutput) {
    // Voices given in reverse order; output must be sorted by ShortName.
    std::vector<Voice> vs = {
        make_voice("zh-CN-XiaoxiaoNeural", VoiceGender::female),
        make_voice("af-ZA-AdriNeural",     VoiceGender::female),
    };

    std::ostringstream out, err;
    std::istringstream in;
    ParseResult r;
    r.action = ParseAction::list_voices;
    r.exit_code = 0;

    EdgeTtsCommandDispatcher d{make_voice_svc(vs), make_factory({}), out, err, in};
    d.dispatch(r);

    const std::string& output = out.str();
    EXPECT_TRUE(output.find("af-ZA") < output.find("zh-CN"));
}

// ---------------------------------------------------------------------------
// list-voices: service error → stderr, exit 1
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, ListVoicesServiceErrorPrintsToStderr) {
    std::ostringstream out, err;
    std::istringstream in;
    ParseResult r;
    r.action = ParseAction::list_voices;
    r.exit_code = 0;

    EdgeTtsCommandDispatcher d{
        make_failing_voice_svc(ErrorCode::network_error, "connection refused"),
        make_factory({}), out, err, in};

    EXPECT_EQ(d.dispatch(r), 1);
    EXPECT_FALSE(err.str().empty());
    EXPECT_NE(err.str().find("connection refused"), std::string::npos);
}

TEST(EdgeTtsCommandDispatcher, ListVoicesServiceErrorDoesNotPrintToStdout) {
    std::ostringstream out, err;
    std::istringstream in;
    ParseResult r;
    r.action = ParseAction::list_voices;

    EdgeTtsCommandDispatcher d{
        make_failing_voice_svc(ErrorCode::service_error, "403"),
        make_factory({}), out, err, in};
    d.dispatch(r);

    EXPECT_TRUE(out.str().empty());
}

// ---------------------------------------------------------------------------
// text synthesis: factory receives correct text
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, TextSynthesisCallsFactory) {
    std::string received_text;
    auto factory = [&received_text](std::string text, TtsConfig cfg, SynthesisOptions) {
        received_text = text;
        return SpeechSynthesizer(std::move(text), std::move(cfg),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher d{make_voice_svc({}), factory, out, err, in};
    d.dispatch(make_text_result("hello world"));

    EXPECT_EQ(received_text, "hello world");
}

TEST(EdgeTtsCommandDispatcher, TextSynthesisExitCodeSuccess) {
    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory({}), out, err, in};
    EXPECT_EQ(d.dispatch(make_text_result("hello")), 0);
}

// ---------------------------------------------------------------------------
// file synthesis: InputLoader reads file, factory receives file content
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, FileSynthesisLoadsFile) {
    const fs::path p = tmp_path("file_synth.txt");
    FileGuard g{p};
    { std::ofstream f(p); f << "from file"; }

    std::string received_text;
    auto factory = [&received_text](std::string text, TtsConfig cfg, SynthesisOptions) {
        received_text = text;
        return SpeechSynthesizer(std::move(text), std::move(cfg),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };

    std::ostringstream out, err;
    std::istringstream in;

    ParseResult r;
    r.action = ParseAction::synthesize;
    r.exit_code = 0;
    r.arguments.file = p.string();

    EdgeTtsCommandDispatcher d{make_voice_svc({}), factory, out, err, in};
    int rc = d.dispatch(r);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(received_text, "from file");
}

TEST(EdgeTtsCommandDispatcher, FileSynthesisMissingFileReturnsError) {
    std::ostringstream out, err;
    std::istringstream in;

    ParseResult r;
    r.action = ParseAction::synthesize;
    r.exit_code = 0;
    r.arguments.file = "/no/such/file.txt";

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory({}), out, err, in};
    EXPECT_EQ(d.dispatch(r), 1);
    EXPECT_FALSE(err.str().empty());
}

// ---------------------------------------------------------------------------
// audio → stdout when write_media absent
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, AudioWrittenToStdoutWhenNoWriteMedia) {
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("mp3data")}};
    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};
    d.dispatch(make_text_result("hello"));

    EXPECT_EQ(out.str(), "mp3data");
}

TEST(EdgeTtsCommandDispatcher, AudioWrittenToStdoutWhenWriteMediaIsDash) {
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("audio")}};
    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};
    d.dispatch(make_text_result("hello", "-"));

    EXPECT_EQ(out.str(), "audio");
}

// ---------------------------------------------------------------------------
// write-media: audio written to file
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, WriteMediaWritesAudioFile) {
    const fs::path mp = tmp_path("write_media.mp3");
    FileGuard gm{mp};

    std::vector<TtsChunk> chunks{
        TtsChunk{make_audio("PART1")},
        TtsChunk{make_audio("PART2")},
    };
    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};
    int rc = d.dispatch(make_text_result("hello", mp.string()));

    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(fs::exists(mp));

    const auto bytes = read_binary_file(mp);
    const std::string content(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    EXPECT_EQ(content, "PART1PART2");
}

TEST(EdgeTtsCommandDispatcher, WriteMediaFileDoesNotPolluteSstdout) {
    const fs::path mp = tmp_path("write_media_no_stdout.mp3");
    FileGuard gm{mp};

    std::vector<TtsChunk> chunks{TtsChunk{make_audio("audio")}};
    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};
    d.dispatch(make_text_result("hello", mp.string()));

    EXPECT_TRUE(out.str().empty());
}

// ---------------------------------------------------------------------------
// write-subtitles: SRT routing
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, WriteSubtitlesDashWritesToStderr) {
    std::vector<TtsChunk> chunks{
        TtsChunk{make_audio("mp3")},
        TtsChunk{make_boundary("Hello world", 0, 10'000'000)},
    };
    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};
    int rc = d.dispatch(make_text_result("Hello world", {}, "-"));

    EXPECT_EQ(rc, 0);
    // SRT must appear on stderr.
    EXPECT_NE(err.str().find("-->"), std::string::npos);
}

TEST(EdgeTtsCommandDispatcher, WriteSubtitlesToFile) {
    const fs::path srt = tmp_path("write_srt.srt");
    FileGuard gs{srt};

    std::vector<TtsChunk> chunks{
        TtsChunk{make_audio("mp3")},
        TtsChunk{make_boundary("Hello world", 0, 10'000'000)},
    };
    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};
    int rc = d.dispatch(make_text_result("Hello world", {}, srt.string()));

    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(fs::exists(srt));
    EXPECT_NE(read_text_file(srt).find("-->"), std::string::npos);
}

TEST(EdgeTtsCommandDispatcher, NoSubtitlePathProducesNoSrtOutput) {
    std::vector<TtsChunk> chunks{
        TtsChunk{make_audio("mp3")},
        TtsChunk{make_boundary("Hello", 0, 10'000'000)},
    };
    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};
    d.dispatch(make_text_result("Hello")); // no subtitle path

    // No SRT marker on either stream.
    EXPECT_EQ(err.str().find("-->"), std::string::npos);
}

// ---------------------------------------------------------------------------
// synthesis error → stderr, exit 1
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, SynthesisErrorPrintsToStderr) {
    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{
        make_voice_svc({}),
        make_failing_factory(ErrorCode::network_error, "connection failed"),
        out, err, in};

    int rc = d.dispatch(make_text_result("hello"));
    EXPECT_EQ(rc, 1);
    EXPECT_NE(err.str().find("connection failed"), std::string::npos);
}

TEST(EdgeTtsCommandDispatcher, SynthesisErrorDoesNotWriteToStdout) {
    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{
        make_voice_svc({}),
        make_failing_factory(ErrorCode::service_error, "no audio"),
        out, err, in};

    d.dispatch(make_text_result("hello"));
    EXPECT_TRUE(out.str().empty());
}

// ---------------------------------------------------------------------------
// Exit-code taxonomy — one test per remaining ErrorCode.  All runtime errors
// map to exit 1.  See docs/MODULES.md — CLI exit code mapping.
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, ProtocolErrorReturnsExit1) {
    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher d{
        make_voice_svc({}),
        make_failing_factory(ErrorCode::protocol_error, "unexpected message path"),
        out, err, in};
    EXPECT_EQ(d.dispatch(make_text_result("hello")), 1);
    EXPECT_FALSE(err.str().empty());
}

TEST(EdgeTtsCommandDispatcher, ParseErrorReturnsExit1) {
    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher d{
        make_voice_svc({}),
        make_failing_factory(ErrorCode::parse_error, "invalid JSON in metadata"),
        out, err, in};
    EXPECT_EQ(d.dispatch(make_text_result("hello")), 1);
    EXPECT_FALSE(err.str().empty());
}

TEST(EdgeTtsCommandDispatcher, TimeoutErrorReturnsExit1) {
    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher d{
        make_voice_svc({}),
        make_failing_factory(ErrorCode::timeout, "receive timeout after 60 s"),
        out, err, in};
    EXPECT_EQ(d.dispatch(make_text_result("hello")), 1);
    EXPECT_FALSE(err.str().empty());
}

TEST(EdgeTtsCommandDispatcher, DrmErrorReturnsExit1) {
    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher d{
        make_voice_svc({}),
        make_failing_factory(ErrorCode::drm_error, "HTTP 403 after retry"),
        out, err, in};
    EXPECT_EQ(d.dispatch(make_text_result("hello")), 1);
    EXPECT_FALSE(err.str().empty());
}

TEST(EdgeTtsCommandDispatcher, CancelledErrorReturnsExit1) {
    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher d{
        make_voice_svc({}),
        make_failing_factory(ErrorCode::cancelled, "synthesis was cancelled"),
        out, err, in};
    EXPECT_EQ(d.dispatch(make_text_result("hello")), 1);
    EXPECT_FALSE(err.str().empty());
}

// ---------------------------------------------------------------------------
// help / version / error actions (dispatched by EdgeTtsCommandDispatcher)
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, HelpPrintsToStdoutAndReturns0) {
    EdgeTtsArgumentParser parser;
    auto result = parser.parse({"--help"});

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory({}), out, err, in};

    EXPECT_EQ(d.dispatch(result), 0);
    EXPECT_NE(out.str().find("Usage"), std::string::npos);
}

TEST(EdgeTtsCommandDispatcher, VersionPrintsToStdoutAndReturns0) {
    EdgeTtsArgumentParser parser;
    auto result = parser.parse({"--version"});

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory({}), out, err, in};

    EXPECT_EQ(d.dispatch(result), 0);
    EXPECT_NE(out.str().find("edge-tts-cpp"), std::string::npos);
}

TEST(EdgeTtsCommandDispatcher, ErrorActionPrintsToStderrAndReturns2) {
    EdgeTtsArgumentParser parser;
    auto result = parser.parse({}); // no args → error

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory({}), out, err, in};

    EXPECT_EQ(d.dispatch(result), 2);
    EXPECT_FALSE(err.str().empty());
}

// ---------------------------------------------------------------------------
// write-media file error → stderr, exit 1, filename in error message
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, WriteMediaFileErrorReturnsFailure) {
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("mp3")}};
    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};
    // Write to a path with a non-existent parent.
    int rc = d.dispatch(make_text_result("hi", "/no_such_dir/out.mp3"));

    EXPECT_EQ(rc, 1);
    EXPECT_FALSE(err.str().empty());
}

TEST(EdgeTtsCommandDispatcher, WriteMediaFileErrorIncludesFilenameInStderr) {
    // The io_error from FileWriter carries the path in its context; the
    // dispatcher must forward it so the user knows which file failed.
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("mp3")}};
    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};
    int rc = d.dispatch(make_text_result("hi", "/no_such_dir/out.mp3"));

    EXPECT_EQ(rc, 1);
    EXPECT_NE(err.str().find("out.mp3"), std::string::npos);
}

// ---------------------------------------------------------------------------
// proxy option is set in SynthesisOptions
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, ProxyOptionIsSetInSynthesisOptions) {
    SynthesisOptions received_opts;
    auto factory = [&received_opts](std::string text, TtsConfig cfg, SynthesisOptions opts) {
        received_opts = opts;
        return SpeechSynthesizer(std::move(text), std::move(cfg), std::move(opts),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };

    std::ostringstream out, err;
    std::istringstream in;

    ParseResult r = make_text_result("hello");
    r.arguments.proxy = "http://proxy.test:3128";

    EdgeTtsCommandDispatcher d{make_voice_svc({}), factory, out, err, in};
    d.dispatch(r);

    EXPECT_EQ(received_opts.proxy, "http://proxy.test:3128");
}

TEST(EdgeTtsCommandDispatcher, ProxyAbsentWhenNotProvided) {
    SynthesisOptions received_opts;
    received_opts.proxy = "should-be-cleared";
    auto factory = [&received_opts](std::string text, TtsConfig cfg, SynthesisOptions opts) {
        received_opts = opts;
        return SpeechSynthesizer(std::move(text), std::move(cfg), std::move(opts),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };

    std::ostringstream out, err;
    std::istringstream in;

    ParseResult r = make_text_result("hello"); // no proxy
    EdgeTtsCommandDispatcher d{make_voice_svc({}), factory, out, err, in};
    d.dispatch(r);

    EXPECT_FALSE(received_opts.proxy.has_value());
}

// ---------------------------------------------------------------------------
// Proxy: rejected early → exit 1, error on stderr
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, ProxyYieldsExitCode1AndErrorOnStderr) {
    // Proxy is rejected at the API layer before the synthesizer function runs.
    // The dispatcher must propagate the error: exit code 1, message on stderr.
    auto factory = [](std::string text, TtsConfig cfg, SynthesisOptions opts) {
        return SpeechSynthesizer(std::move(text), std::move(cfg), std::move(opts),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>>
            {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };
    std::ostringstream out, err;
    std::istringstream in;
    ParseResult r = make_text_result("hello");
    r.arguments.proxy = "http://proxy.test:3128";
    EdgeTtsCommandDispatcher d{make_voice_svc({}), factory, out, err, in};
    int code = d.dispatch(r);
    EXPECT_EQ(code, 1);
    EXPECT_FALSE(err.str().empty());
}

TEST(EdgeTtsCommandDispatcher, ProxyRejectedBeforeSynthesizerFn) {
    // Proxy is rejected at the API layer — the synthesizer function must
    // never be called when proxy is set.
    bool fn_called = false;
    auto factory = [&fn_called](std::string text, TtsConfig cfg, SynthesisOptions opts) {
        return SpeechSynthesizer(std::move(text), std::move(cfg), std::move(opts),
            [&fn_called](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>>
            {
                fn_called = true;
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };
    std::ostringstream out, err;
    std::istringstream in;
    ParseResult r = make_text_result("hello");
    r.arguments.proxy = "http://proxy.test:3128";
    EdgeTtsCommandDispatcher d{make_voice_svc({}), factory, out, err, in};
    int code = d.dispatch(r);
    EXPECT_NE(code, 0);
    EXPECT_FALSE(fn_called);
}

// ---------------------------------------------------------------------------
// SubtitleBuilder::feed errors → stderr, exit 1
//
// SubtitleBuilder locks the boundary type on the first feed().  Mixing WordBoundary
// and SentenceBoundary in the same session triggers invalid_argument.
// ---------------------------------------------------------------------------

static BoundaryChunk make_boundary_of_type(BoundaryEventType type,
                                           std::string       text,
                                           std::int64_t      offset   = 0,
                                           std::int64_t      duration = 10'000'000) {
    BoundaryChunk bc;
    bc.type           = type;
    bc.text           = std::move(text);
    bc.offset_ticks   = offset;
    bc.duration_ticks = duration;
    return bc;
}

TEST(EdgeTtsCommandDispatcher, SubtitleFeedTypeMismatchReturnsError) {
    // Feed a WordBoundary then a SentenceBoundary — SubtitleBuilder rejects the second.
    std::vector<TtsChunk> chunks{
        TtsChunk{make_audio("mp3")},
        TtsChunk{make_boundary_of_type(BoundaryEventType::WordBoundary,    "Hello", 0,          10'000'000)},
        TtsChunk{make_boundary_of_type(BoundaryEventType::SentenceBoundary,"Hello", 10'000'000, 10'000'000)},
    };
    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};
    int rc = d.dispatch(make_text_result("hello", {}, "-"));

    EXPECT_EQ(rc, 1);
    EXPECT_FALSE(err.str().empty());
}

TEST(EdgeTtsCommandDispatcher, SubtitleFeedErrorPrintsMessageToStderr) {
    // Verify the error message reaches stderr (stdout content may already have
    // audio that was streamed before the conflict was detected).
    std::vector<TtsChunk> chunks{
        TtsChunk{make_audio("mp3")},
        TtsChunk{make_boundary_of_type(BoundaryEventType::WordBoundary,    "A", 0,          10'000'000)},
        TtsChunk{make_boundary_of_type(BoundaryEventType::SentenceBoundary,"A", 10'000'000, 10'000'000)},
    };
    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};
    int rc = d.dispatch(make_text_result("hello", {}, "-"));

    EXPECT_EQ(rc, 1);
    EXPECT_FALSE(err.str().empty());
}

// ---------------------------------------------------------------------------
// Interactive TTY warning
//
// The warning fires only when:
//   - tty_check_ is set AND returns true
//   - --write-media is absent (not even "-")
//
// After the warning is printed, the dispatcher reads one line from in_.
// If getline succeeds → synthesis proceeds normally.
// If getline fails (EOF) → "Operation canceled." on stderr, return 0.
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, TtyWarningPrintedToStderr) {
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("audio")}};
    std::ostringstream out, err;
    std::istringstream in("\n"); // simulate Enter key

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in,
                                []{ return true; }};
    int rc = d.dispatch(make_text_result("hello")); // no --write-media

    EXPECT_EQ(rc, 0);
    // Warning must be on stderr.
    EXPECT_NE(err.str().find("terminal"), std::string::npos);
    EXPECT_NE(err.str().find("Enter"),    std::string::npos);
}

TEST(EdgeTtsCommandDispatcher, TtyWarningAllowsSynthesisAfterEnter) {
    // Synthesis must still produce audio after the user presses Enter.
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("AUDIODATA")}};
    std::ostringstream out, err;
    std::istringstream in("\n"); // Enter key

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in,
                                []{ return true; }};
    d.dispatch(make_text_result("hello"));

    EXPECT_EQ(out.str(), "AUDIODATA");
}

TEST(EdgeTtsCommandDispatcher, TtyWarningCancelsOnEof) {
    // EOF on stdin (e.g. Ctrl-C on real terminal) → "Operation canceled." + exit 0.
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("audio")}};
    std::ostringstream out, err;
    std::istringstream in(""); // EOF immediately

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in,
                                []{ return true; }};
    int rc = d.dispatch(make_text_result("hello"));

    EXPECT_EQ(rc, 0);
    EXPECT_NE(err.str().find("canceled"), std::string::npos);
    // No audio must be written on cancellation.
    EXPECT_TRUE(out.str().empty());
}

TEST(EdgeTtsCommandDispatcher, TtyWarningNotShownWhenTtyCheckFalse) {
    // tty_check returns false → no warning, synthesis proceeds immediately.
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("audio")}};
    std::ostringstream out, err;
    std::istringstream in; // empty — would block if warning tried to read

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in,
                                []{ return false; }};
    int rc = d.dispatch(make_text_result("hello"));

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(err.str().find("terminal"), std::string::npos);
}

TEST(EdgeTtsCommandDispatcher, TtyWarningNotShownWhenWriteMediaIsDash) {
    // --write-media=- selects stdout explicitly; user knowingly chose stdout.
    // --write-media=- selects stdout explicitly; write_media is set, so no warning.
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("audio")}};
    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in,
                                []{ return true; }};
    int rc = d.dispatch(make_text_result("hello", "-")); // --write-media=-

    EXPECT_EQ(rc, 0);
    // Warning must NOT appear when write_media is "-".
    EXPECT_EQ(err.str().find("terminal"), std::string::npos);
}

TEST(EdgeTtsCommandDispatcher, TtyWarningNotShownWhenWriteMediaIsFile) {
    // --write-media=PATH → audio goes to file, not stdout → no TTY warning.
    const fs::path mp = tmp_path("tty_no_warn.mp3");
    FileGuard gm{mp};

    std::vector<TtsChunk> chunks{TtsChunk{make_audio("audio")}};
    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in,
                                []{ return true; }};
    int rc = d.dispatch(make_text_result("hello", mp.string()));

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(err.str().find("terminal"), std::string::npos);
}

TEST(EdgeTtsCommandDispatcher, TtyWarningNotShownWhenCheckFnIsEmpty) {
    // Default constructor (no tty_check): no warning ever, no hang.
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("audio")}};
    std::ostringstream out, err;
    std::istringstream in;

    // Construct WITHOUT tty_check (default = empty function = disabled).
    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};
    int rc = d.dispatch(make_text_result("hello"));

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(err.str().find("terminal"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Proxy credential redaction
//
// When synthesis fails and the error context carries a proxy URL that contains
// embedded credentials (user:password@host), the dispatcher must not expose the
// raw credential string in its stderr output.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// TTS config (voice, rate, volume, pitch) forwarded to factory
//
// dispatch_synthesize() builds a TtsConfig from the CLI arguments and passes
// it to the SynthesizerFactory.  Each field must be forwarded without mutation.
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, VoiceForwardedToFactory) {
    TtsConfig received_cfg;
    auto factory = [&received_cfg](std::string text, TtsConfig cfg, SynthesisOptions) {
        received_cfg = cfg;
        return SpeechSynthesizer(std::move(text), std::move(cfg),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };
    std::ostringstream out, err;
    std::istringstream in;

    ParseResult r = make_text_result("hi");
    r.arguments.voice = "en-GB-RyanNeural";

    EdgeTtsCommandDispatcher d{make_voice_svc({}), factory, out, err, in};
    d.dispatch(r);

    EXPECT_EQ(received_cfg.voice, "en-GB-RyanNeural");
}

TEST(EdgeTtsCommandDispatcher, RateForwardedToFactory) {
    TtsConfig received_cfg;
    auto factory = [&received_cfg](std::string text, TtsConfig cfg, SynthesisOptions) {
        received_cfg = cfg;
        return SpeechSynthesizer(std::move(text), std::move(cfg),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };
    std::ostringstream out, err;
    std::istringstream in;

    ParseResult r = make_text_result("hi");
    r.arguments.rate = "+50%";

    EdgeTtsCommandDispatcher d{make_voice_svc({}), factory, out, err, in};
    d.dispatch(r);

    EXPECT_EQ(received_cfg.rate, "+50%");
}

TEST(EdgeTtsCommandDispatcher, VolumeForwardedToFactory) {
    TtsConfig received_cfg;
    auto factory = [&received_cfg](std::string text, TtsConfig cfg, SynthesisOptions) {
        received_cfg = cfg;
        return SpeechSynthesizer(std::move(text), std::move(cfg),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };
    std::ostringstream out, err;
    std::istringstream in;

    ParseResult r = make_text_result("hi");
    r.arguments.volume = "+20%";

    EdgeTtsCommandDispatcher d{make_voice_svc({}), factory, out, err, in};
    d.dispatch(r);

    EXPECT_EQ(received_cfg.volume, "+20%");
}

TEST(EdgeTtsCommandDispatcher, PitchForwardedToFactory) {
    TtsConfig received_cfg;
    auto factory = [&received_cfg](std::string text, TtsConfig cfg, SynthesisOptions) {
        received_cfg = cfg;
        return SpeechSynthesizer(std::move(text), std::move(cfg),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };
    std::ostringstream out, err;
    std::istringstream in;

    ParseResult r = make_text_result("hi");
    r.arguments.pitch = "+10Hz";

    EdgeTtsCommandDispatcher d{make_voice_svc({}), factory, out, err, in};
    d.dispatch(r);

    EXPECT_EQ(received_cfg.pitch, "+10Hz");
}

// ---------------------------------------------------------------------------
// Binary audio output — bytes with values > 127 must reach stdout uncorrupted
//
// The dispatcher uses out_.write(ptr, size) for audio, not operator<<, so
// binary bytes with high values must pass through without text-mode
// translation.  We inject a known byte pattern including 0x00, 0x80, 0xFF
// and verify the exact bytes appear in the stream.
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, BinaryAudioBytesNotCorrupted) {
    // Build an AudioChunk containing every interesting byte boundary.
    AudioChunk ac;
    ac.data = {
        std::byte{0x00},  // null byte
        std::byte{0x01},
        std::byte{0x7F},  // max ASCII
        std::byte{0x80},  // first high byte
        std::byte{0xFE},
        std::byte{0xFF},  // max byte
    };
    std::vector<TtsChunk> chunks{TtsChunk{ac}};

    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};
    int rc = d.dispatch(make_text_result("hello"));

    EXPECT_EQ(rc, 0);
    const std::string result = out.str();
    ASSERT_EQ(result.size(), 6u);
    EXPECT_EQ(static_cast<unsigned char>(result[0]), 0x00u);
    EXPECT_EQ(static_cast<unsigned char>(result[1]), 0x01u);
    EXPECT_EQ(static_cast<unsigned char>(result[2]), 0x7Fu);
    EXPECT_EQ(static_cast<unsigned char>(result[3]), 0x80u);
    EXPECT_EQ(static_cast<unsigned char>(result[4]), 0xFEu);
    EXPECT_EQ(static_cast<unsigned char>(result[5]), 0xFFu);
}

TEST(EdgeTtsCommandDispatcher, BinaryAudioBytesNotCorruptedWhenRoutedToFile) {
    // Same byte pattern, but routed to a file via --write-media.
    const fs::path mp = tmp_path("binary_exact.mp3");
    FileGuard gm{mp};

    AudioChunk ac;
    ac.data = {std::byte{0x00}, std::byte{0x80}, std::byte{0xFF}};
    std::vector<TtsChunk> chunks{TtsChunk{ac}};

    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};
    d.dispatch(make_text_result("hello", mp.string()));

    const auto bytes = read_binary_file(mp);
    ASSERT_EQ(bytes.size(), 3u);
    EXPECT_EQ(bytes[0], std::byte{0x00});
    EXPECT_EQ(bytes[1], std::byte{0x80});
    EXPECT_EQ(bytes[2], std::byte{0xFF});
}

// ---------------------------------------------------------------------------
// Subtitle file write error → stderr, exit 1
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, WriteSubtitlesFileErrorReturnsFailure) {
    std::vector<TtsChunk> chunks{
        TtsChunk{make_audio("mp3")},
        TtsChunk{make_boundary("Hello world", 0, 10'000'000)},
    };
    std::ostringstream out, err;
    std::istringstream in;

    // Route subtitles to a path with a non-existent parent directory.
    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};
    int rc = d.dispatch(make_text_result("Hello world", {}, "/no_such_dir/out.srt"));

    EXPECT_EQ(rc, 1);
    EXPECT_FALSE(err.str().empty());
}

TEST(EdgeTtsCommandDispatcher, WriteSubtitlesFileErrorIncludesFilenameInStderr) {
    std::vector<TtsChunk> chunks{
        TtsChunk{make_audio("mp3")},
        TtsChunk{make_boundary("Hello world", 0, 10'000'000)},
    };
    std::ostringstream out, err;
    std::istringstream in;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};
    int rc = d.dispatch(make_text_result("Hello world", {}, "/no_such_dir/out.srt"));

    EXPECT_EQ(rc, 1);
    EXPECT_NE(err.str().find("out.srt"), std::string::npos);
}

// ---------------------------------------------------------------------------
// --file=- reads text from the injected stdin stream
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, FileDashReadsFromStdin) {
    std::string received_text;
    auto factory = [&received_text](std::string text, TtsConfig cfg, SynthesisOptions) {
        received_text = text;
        return SpeechSynthesizer(std::move(text), std::move(cfg),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };

    std::ostringstream out, err;
    std::istringstream in("stdin content");

    ParseResult r;
    r.action    = ParseAction::synthesize;
    r.exit_code = 0;
    r.arguments.file = "-";  // --file=-

    EdgeTtsCommandDispatcher d{make_voice_svc({}), factory, out, err, in};
    int rc = d.dispatch(r);

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(received_text, "stdin content");
}

TEST(EdgeTtsCommandDispatcher, FileDevStdinReadsFromStdin) {
    std::string received_text;
    auto factory = [&received_text](std::string text, TtsConfig cfg, SynthesisOptions) {
        received_text = text;
        return SpeechSynthesizer(std::move(text), std::move(cfg),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };

    std::ostringstream out, err;
    std::istringstream in("dev stdin content");

    ParseResult r;
    r.action    = ParseAction::synthesize;
    r.exit_code = 0;
    r.arguments.file = "/dev/stdin";

    EdgeTtsCommandDispatcher d{make_voice_svc({}), factory, out, err, in};
    int rc = d.dispatch(r);

    EXPECT_EQ(rc, 0);
    EXPECT_EQ(received_text, "dev stdin content");
}

// ---------------------------------------------------------------------------
// Empty voice list — list-voices with zero voices must succeed and produce
// some output (at minimum the header row of the table).
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, ListVoicesEmptyListSucceeds) {
    std::ostringstream out, err;
    std::istringstream in;
    ParseResult r;
    r.action    = ParseAction::list_voices;
    r.exit_code = 0;

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory({}), out, err, in};
    int rc = d.dispatch(r);

    EXPECT_EQ(rc, 0);
    // stdout must not be contaminated with errors.
    EXPECT_TRUE(err.str().empty());
}

// ---------------------------------------------------------------------------
// Proxy credential redaction
//
// When synthesis fails and the error context carries a proxy URL that contains
// embedded credentials (user:password@host), the dispatcher must not expose the
// raw credential string in its stderr output.
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, ProxyCredentialNotExposedInStderr) {
    // When a proxy URL with embedded credentials is provided, credentials must
    // not appear in stderr.  Proxy is rejected at the API layer before the
    // synthesizer function runs, so only a "proxy is not supported" message
    // appears — no URL, no credentials.
    auto factory = [](std::string text, TtsConfig cfg, SynthesisOptions opts) {
        return SpeechSynthesizer(std::move(text), std::move(cfg), std::move(opts),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };
    std::ostringstream out, err;
    std::istringstream in;
    ParseResult r = make_text_result("hello");
    r.arguments.proxy = "http://user:secretpassword@proxy.internal:3128";
    EdgeTtsCommandDispatcher d{make_voice_svc({}), factory, out, err, in};
    d.dispatch(r);

    EXPECT_EQ(err.str().find("secretpassword"), std::string::npos);
    EXPECT_NE(err.str().find("proxy"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Empty text input: exit 0, no audio — not a parse error (exit 2).
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, EmptyTextSynthesisSucceeds) {
    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory({}), out, err, in};

    ParseResult r = make_text_result("");
    int rc = d.dispatch(r);

    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(err.str().empty());
}

TEST(EdgeTtsCommandDispatcher, EmptyTextNoAudioWrittenToStdout) {
    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory({}), out, err, in};

    ParseResult r = make_text_result("");
    d.dispatch(r);

    EXPECT_TRUE(out.str().empty());
}

// ---------------------------------------------------------------------------
// Missing input file: io_error carries path in context → path appears in stderr.
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, FileSynthesisMissingFileErrorIncludesPath) {
    std::ostringstream out, err;
    std::istringstream in;

    ParseResult r;
    r.action    = ParseAction::synthesize;
    r.exit_code = 0;
    r.arguments.file = "/no/such/input_file_unique.txt";

    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory({}), out, err, in};
    EXPECT_EQ(d.dispatch(r), 1);
    EXPECT_NE(err.str().find("input_file_unique.txt"), std::string::npos);
}

// ---------------------------------------------------------------------------
// invalid_argument from synthesis → exit 1 (not exit 2; parser accepts any
// string for voice/rate/pitch/volume, validation happens at synthesis time).
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, InvalidArgumentFromSynthesisReturns1) {
    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher d{
        make_voice_svc({}),
        make_failing_factory(ErrorCode::invalid_argument, "unknown voice 'bad-voice'"),
        out, err, in};

    EXPECT_EQ(d.dispatch(make_text_result("hello")), 1);
}

TEST(EdgeTtsCommandDispatcher, InvalidArgumentFromSynthesisPrintsMessageToStderr) {
    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher d{
        make_voice_svc({}),
        make_failing_factory(ErrorCode::invalid_argument, "unknown voice 'bad-voice'"),
        out, err, in};

    d.dispatch(make_text_result("hello"));
    EXPECT_FALSE(err.str().empty());
}

// ---------------------------------------------------------------------------
// --write-media silently overwrites an existing file (no --force guard).
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, WriteMediaOverwritesExistingFile) {
    const fs::path out_path = tmp_path("overwrite_test.mp3");
    FileGuard guard{out_path};

    // Pre-create the file with different content.
    { std::ofstream f(out_path); f << "OLD CONTENT"; }

    std::vector<TtsChunk> chunks{TtsChunk{make_audio("NEW")}};
    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};

    int rc = d.dispatch(make_text_result("hello", out_path.string()));

    EXPECT_EQ(rc, 0);
    const auto written = read_binary_file(out_path);
    EXPECT_NE(std::string(reinterpret_cast<const char*>(written.data()),
                          written.size()).find("NEW"), std::string::npos);
}

// ---------------------------------------------------------------------------
// --write-media path is an existing directory → exit 1, path in stderr.
//
// Attempting to write audio to a path that already exists as a directory
// must fail cleanly.  The dispatcher must not silently succeed (writing
// nothing) or crash; it must report an io_error and include the path in
// stderr so the user can identify the bad argument.
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, WriteMediaToDirectoryReturns1) {
    // Use the system temp directory as a guaranteed-existing directory target.
    const std::string dir_path = fs::temp_directory_path().string();

    std::vector<TtsChunk> chunks{TtsChunk{make_audio("mp3")}};
    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory(chunks), out, err, in};

    EXPECT_EQ(d.dispatch(make_text_result("hello", dir_path)), 1);
    EXPECT_FALSE(err.str().empty());
}

// ---------------------------------------------------------------------------
// --file path is an existing directory → exit 1, path in stderr.
//
// Passing a directory as the input file must fail with io_error.  The
// InputLoader attempts to open the path as a regular file, which fails on a
// directory; the error context must contain the path.
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, FileInputFromDirectoryReturns1) {
    const std::string dir_path = fs::temp_directory_path().string();

    ParseResult r;
    r.action    = ParseAction::synthesize;
    r.exit_code = 0;
    r.arguments.file = dir_path;

    std::ostringstream out, err;
    std::istringstream in;
    EdgeTtsCommandDispatcher d{make_voice_svc({}), make_factory({}), out, err, in};

    EXPECT_EQ(d.dispatch(r), 1);
    EXPECT_FALSE(err.str().empty());
}
