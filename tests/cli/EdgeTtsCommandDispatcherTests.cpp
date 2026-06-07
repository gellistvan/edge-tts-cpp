#include "edge_tts/cli/EdgeTtsCommandDispatcher.hpp"
#include "edge_tts/cli/EdgeTtsArgumentParser.hpp"
#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/api/CommunicateOptions.hpp"
#include "edge_tts/common/Error.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/core/Voice.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using edge_tts::api::Communicate;
using edge_tts::api::CommunicateOptions;
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

// Build an AudioChunk with the given bytes.
static AudioChunk make_audio(std::string_view s) {
    AudioChunk ac;
    ac.data.reserve(s.size());
    for (char c : s) ac.data.push_back(static_cast<std::byte>(c));
    return ac;
}

// Build a BoundaryChunk.
static BoundaryChunk make_boundary(std::string text,
                                   std::int64_t offset  = 0,
                                   std::int64_t duration = 10'000'000) {
    BoundaryChunk bc;
    bc.type           = BoundaryEventType::SentenceBoundary;
    bc.text           = std::move(text);
    bc.offset_ticks   = offset;
    bc.duration_ticks = duration;
    return bc;
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

// Factory that creates a Communicate with a fixed response.
static EdgeTtsCommandDispatcher::CommunicateFactory
make_factory(std::vector<TtsChunk> chunks) {
    return [chunks = std::move(chunks)](
               std::string text, TtsConfig cfg, CommunicateOptions opts) {
        return Communicate(std::move(text), std::move(cfg), std::move(opts),
            [chunks](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok(chunks);
            });
    };
}

// Factory that injects a synthesis error.
static EdgeTtsCommandDispatcher::CommunicateFactory
make_failing_factory(ErrorCode code, std::string msg) {
    return [code, msg = std::move(msg)](
               std::string text, TtsConfig cfg, CommunicateOptions opts) {
        return Communicate(std::move(text), std::move(cfg), std::move(opts),
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
    auto factory = [&received_text](std::string text, TtsConfig cfg, CommunicateOptions) {
        received_text = text;
        return Communicate(std::move(text), std::move(cfg),
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
    auto factory = [&received_text](std::string text, TtsConfig cfg, CommunicateOptions) {
        received_text = text;
        return Communicate(std::move(text), std::move(cfg),
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
// proxy is forwarded into CommunicateOptions
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, ProxyIsForwardedToFactory) {
    CommunicateOptions received_opts;
    auto factory = [&received_opts](std::string text, TtsConfig cfg, CommunicateOptions opts) {
        received_opts = opts;
        return Communicate(std::move(text), std::move(cfg), std::move(opts),
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

TEST(EdgeTtsCommandDispatcher, EmptyProxyIsForwardedToFactory) {
    CommunicateOptions received_opts;
    received_opts.proxy = "should-be-cleared";
    auto factory = [&received_opts](std::string text, TtsConfig cfg, CommunicateOptions opts) {
        received_opts = opts;
        return Communicate(std::move(text), std::move(cfg), std::move(opts),
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
// Proxy: runtime unsupported → exit 1, error message on stderr
// ---------------------------------------------------------------------------

TEST(EdgeTtsCommandDispatcher, ProxyUnsupportedYieldsExitCode1AndErrorOnStderr) {
    // When the synthesizer returns unsupported (proxy rejected by the transport
    // layer), the dispatcher must propagate it: exit code 1, message on stderr.
    auto factory = [](std::string text, TtsConfig cfg, CommunicateOptions opts) {
        return Communicate(std::move(text), std::move(cfg), std::move(opts),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>>
            {
                return edge_tts::common::Result<std::vector<TtsChunk>>::fail(
                    edge_tts::common::Error{
                        edge_tts::common::ErrorCode::unsupported,
                        "proxy is not supported by the ixwebsocket networking backend"});
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

TEST(EdgeTtsCommandDispatcher, ProxyIsNotSilentlyIgnored) {
    // A transport error from the synthesizer must NOT be swallowed.
    // The dispatcher must return non-zero when the proxy causes a failure.
    bool synthesizer_ran = false;
    auto factory = [&synthesizer_ran](std::string text, TtsConfig cfg,
                                     CommunicateOptions opts) {
        return Communicate(std::move(text), std::move(cfg), std::move(opts),
            [&synthesizer_ran](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>>
            {
                synthesizer_ran = true;
                return edge_tts::common::Result<std::vector<TtsChunk>>::fail(
                    edge_tts::common::Error{
                        edge_tts::common::ErrorCode::unsupported,
                        "proxy not supported"});
            });
    };
    std::ostringstream out, err;
    std::istringstream in;
    ParseResult r = make_text_result("hello");
    r.arguments.proxy = "http://proxy.test:3128";
    EdgeTtsCommandDispatcher d{make_voice_svc({}), factory, out, err, in};
    int code = d.dispatch(r);
    EXPECT_TRUE(synthesizer_ran);
    EXPECT_NE(code, 0);
}

// ---------------------------------------------------------------------------
// SubMaker::feed errors → stderr, exit 1
//
// SubMaker locks the boundary type on the first feed().  Mixing WordBoundary
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
    // Feed a WordBoundary then a SentenceBoundary — SubMaker rejects the second.
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
    // Python: `not args.write_media` is False when write_media=="-", so no warning.
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

TEST(EdgeTtsCommandDispatcher, ProxyCredentialNotExposedInStderr) {
    // Inject an error whose context is a proxy URL with embedded credentials.
    // The dispatcher must strip "secretpassword" before printing.
    auto factory = [](std::string text, TtsConfig cfg, CommunicateOptions opts) {
        return Communicate(std::move(text), std::move(cfg), std::move(opts),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::fail(
                    edge_tts::common::Error{
                        edge_tts::common::ErrorCode::unsupported,
                        "proxy connection failed",
                        "http://user:secretpassword@proxy.internal:3128"});
            });
    };
    std::ostringstream out, err;
    std::istringstream in;
    ParseResult r = make_text_result("hello");
    r.arguments.proxy = "http://user:secretpassword@proxy.internal:3128";
    EdgeTtsCommandDispatcher d{make_voice_svc({}), factory, out, err, in};
    d.dispatch(r);

    EXPECT_EQ(err.str().find("secretpassword"), std::string::npos);
    // The proxy host should still appear (not redacted) so the user knows which proxy.
    EXPECT_NE(err.str().find("proxy.internal"), std::string::npos);
}
