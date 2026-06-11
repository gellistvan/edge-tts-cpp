#include "cli/PlaybackArguments.hpp"
#include "cli/PlaybackCommandDispatcher.hpp"
#include "api/SpeechSynthesizer.hpp"
#include "api/SynthesisOptions.hpp"
#include "common/Error.hpp"
#include "core/Chunk.hpp"
#include "core/TtsConfig.hpp"
#include "media/AudioConverter.hpp"
#include "support/ChunkTestHelpers.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using edge_tts::api::SpeechSynthesizer;
using edge_tts::api::SynthesizerFn;
using edge_tts::cli::PlaybackArgumentParser;
using edge_tts::cli::PlaybackArguments;
using edge_tts::cli::PlaybackCommandDispatcher;
using edge_tts::cli::PlaybackParseAction;
using edge_tts::cli::PlaybackParseResult;
using edge_tts::common::Error;
using edge_tts::common::ErrorCode;
using edge_tts::core::AudioChunk;
using edge_tts::core::TtsChunk;
using edge_tts::core::TtsConfig;
using edge_tts::test::make_audio;

// ---------------------------------------------------------------------------
// Fake IAudioConverter
// ---------------------------------------------------------------------------

class FakeAudioConverter final : public edge_tts::media::IAudioConverter {
public:
    edge_tts::common::Result<void>
    play_mp3(const fs::path& input) override {
        play_called     = true;
        last_played     = input;
        return play_result;
    }

    edge_tts::common::Result<void>
    convert(const fs::path&, const fs::path&) override {
        return edge_tts::common::Result<void>::ok();
    }

    bool   play_called{false};
    fs::path last_played;
    edge_tts::common::Result<void> play_result{edge_tts::common::Result<void>::ok()};
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

using edge_tts::api::SynthesisOptions;

// SynthesizerFactory that returns audio chunks.
// Captures options so tests can inspect what proxy/timeouts were forwarded.
struct CapturingFactory {
    std::vector<TtsChunk> chunks;
    SynthesisOptions    last_options;

    PlaybackCommandDispatcher::SynthesizerFactory make() {
        return [this](std::string text, TtsConfig cfg, SynthesisOptions opts) {
            last_options = opts;
            return SpeechSynthesizer(std::move(text), std::move(cfg),
                [chunks = chunks](const TtsConfig&, std::span<const std::string>)
                    -> edge_tts::common::Result<std::vector<TtsChunk>> {
                    return edge_tts::common::Result<std::vector<TtsChunk>>::ok(chunks);
                });
        };
    }
};

// Convenience: build a factory that ignores options (most tests don't need them).
static PlaybackCommandDispatcher::SynthesizerFactory
make_factory(std::vector<TtsChunk> chunks) {
    return [chunks = std::move(chunks)](std::string text, TtsConfig cfg,
                                        SynthesisOptions /*opts*/) {
        return SpeechSynthesizer(std::move(text), std::move(cfg),
            [chunks](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok(chunks);
            });
    };
}

// SynthesizerFactory that always fails.
static PlaybackCommandDispatcher::SynthesizerFactory
make_failing_factory(ErrorCode code, std::string msg) {
    return [code, msg = std::move(msg)](std::string text, TtsConfig cfg,
                                        SynthesisOptions /*opts*/) {
        return SpeechSynthesizer(std::move(text), std::move(cfg),
            [code, msg](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::fail(
                    Error{code, msg});
            });
    };
}

// TempFileProvider that returns a known path (and creates the directory).
// For ".srt" suffix, also returns a known path so SRT cleanup tests work.
struct KnownTempProvider {
    fs::path dir;
    int      counter{0};

    explicit KnownTempProvider(const std::string& tag)
        : dir(fs::temp_directory_path() / ("pcd_test_" + tag)) {
        fs::create_directories(dir);
    }
    ~KnownTempProvider() { fs::remove_all(dir); }

    PlaybackCommandDispatcher::TempFileProvider provider() {
        return [this](std::string_view suffix)
                   -> std::optional<fs::path> {
            return dir / ("tmp_" + std::to_string(counter++) + std::string(suffix));
        };
    }

    // Returns a provider that gives an MP3 path but no SRT path.
    PlaybackCommandDispatcher::TempFileProvider provider_no_srt() {
        return [this](std::string_view suffix)
                   -> std::optional<fs::path> {
            if (suffix == ".srt") return std::nullopt;
            return dir / ("tmp_" + std::to_string(counter++) + std::string(suffix));
        };
    }
};

// Build a PlaybackParseResult with text action.
static PlaybackParseResult make_play_result(std::string text) {
    PlaybackParseResult r;
    r.action           = PlaybackParseAction::play;
    r.exit_code        = 0;
    r.arguments.text   = std::move(text);
    return r;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Parser: help
// ---------------------------------------------------------------------------

TEST(PlaybackArgumentParser, HelpFlag) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--help"});
    EXPECT_EQ(r.action, PlaybackParseAction::help);
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.message.find("edge-playback"), std::string::npos);
}

TEST(PlaybackArgumentParser, HelpShortFlag) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"-h"});
    EXPECT_EQ(r.action, PlaybackParseAction::help);
    EXPECT_EQ(r.exit_code, 0);
}

TEST(PlaybackArgumentParser, HelpMentionsSeeEdgeTts) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--help"});
    EXPECT_NE(r.message.find("edge-tts"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Parser: text argument
// ---------------------------------------------------------------------------

TEST(PlaybackArgumentParser, TextArgument) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hello world"});
    EXPECT_EQ(r.action, PlaybackParseAction::play);
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.arguments.text.has_value());
    EXPECT_EQ(*r.arguments.text, "hello world");
}

TEST(PlaybackArgumentParser, TextShortFlag) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"-t", "hi"});
    EXPECT_EQ(r.action, PlaybackParseAction::play);
    EXPECT_TRUE(r.arguments.text.has_value());
    EXPECT_EQ(*r.arguments.text, "hi");
}

TEST(PlaybackArgumentParser, FileArgument) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--file", "/tmp/input.txt"});
    EXPECT_EQ(r.action, PlaybackParseAction::play);
    EXPECT_TRUE(r.arguments.file.has_value());
    EXPECT_EQ(*r.arguments.file, "/tmp/input.txt");
}

TEST(PlaybackArgumentParser, MissingTextOrFileIsError) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(PlaybackArgumentParser, TextAndFileMutuallyExclusive) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--file", "f.txt"});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
}

TEST(PlaybackArgumentParser, ListVoicesNotAccepted) {
    PlaybackArgumentParser parser;
    // --list-voices is not supported by edge-playback.
    auto r = parser.parse({"--text", "hi", "--list-voices"});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
}

TEST(PlaybackArgumentParser, WriteMediaNotAccepted) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--write-media", "out.mp3"});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
}

TEST(PlaybackArgumentParser, VoiceOption) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--voice", "en-GB-SoniaNeural"});
    EXPECT_EQ(r.action, PlaybackParseAction::play);
    EXPECT_EQ(r.arguments.voice, "en-GB-SoniaNeural");
}

TEST(PlaybackArgumentParser, MpvFlag) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--mpv"});
    EXPECT_EQ(r.action, PlaybackParseAction::play);
    EXPECT_TRUE(r.arguments.use_mpv);
}

// ---------------------------------------------------------------------------
// Dispatcher: help action
// ---------------------------------------------------------------------------

TEST(PlaybackCommandDispatcher, HelpPrintsToStdoutAndReturns0) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"help"};
    std::ostringstream out, err;
    std::istringstream in;

    PlaybackCommandDispatcher d{make_factory({}), conv, tp.provider(),
                                 false, out, err, in};

    PlaybackParseResult r;
    r.action    = PlaybackParseAction::help;
    r.message   = "Usage: edge-playback ...\n";
    r.exit_code = 0;

    EXPECT_EQ(d.dispatch(r), 0);
    EXPECT_NE(out.str().find("Usage"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Dispatcher: error action
// ---------------------------------------------------------------------------

TEST(PlaybackCommandDispatcher, ErrorPrintsToStderrAndReturns2) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"err"};
    std::ostringstream out, err;
    std::istringstream in;

    PlaybackCommandDispatcher d{make_factory({}), conv, tp.provider(),
                                 false, out, err, in};

    PlaybackParseResult r;
    r.action    = PlaybackParseAction::error;
    r.message   = "bad argument";
    r.exit_code = 2;

    EXPECT_EQ(d.dispatch(r), 2);
    EXPECT_NE(err.str().find("bad argument"), std::string::npos);
    EXPECT_TRUE(out.str().empty());
}

// ---------------------------------------------------------------------------
// Dispatcher: synthesis called with correct text
// ---------------------------------------------------------------------------

TEST(PlaybackCommandDispatcher, SynthesisCalledWithCorrectText) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"synth_text"};

    std::string received_text;
    auto factory = [&received_text](std::string text, TtsConfig cfg,
                                    SynthesisOptions /*opts*/) {
        received_text = text;
        return SpeechSynthesizer(std::move(text), std::move(cfg),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };

    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{factory, conv, tp.provider(), false, out, err, in};
    d.dispatch(make_play_result("say this please"));

    EXPECT_EQ(received_text, "say this please");
}

// ---------------------------------------------------------------------------
// Dispatcher: playback called
// ---------------------------------------------------------------------------

TEST(PlaybackCommandDispatcher, PlaybackIsCalled) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"play_called"};
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("mp3bytes")}};

    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{make_factory(chunks), conv, tp.provider(),
                                 false, out, err, in};
    int rc = d.dispatch(make_play_result("hello"));

    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(conv.play_called);
}

// ---------------------------------------------------------------------------
// Dispatcher: temp file lifecycle
// ---------------------------------------------------------------------------

TEST(PlaybackCommandDispatcher, TempFileCleanedOnSuccess) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"cleanup_ok"};
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("audio")}};

    fs::path captured_path;
    auto provider = [&captured_path, &tp](std::string_view suffix)
            -> std::optional<fs::path> {
        if (suffix == ".srt") return std::nullopt;
        captured_path = tp.dir / ("known" + std::string(suffix));
        return captured_path;
    };

    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{make_factory(chunks), conv, std::move(provider),
                                 false, out, err, in};
    EXPECT_EQ(d.dispatch(make_play_result("hi")), 0);

    // Temp file must be deleted after successful playback.
    EXPECT_FALSE(fs::exists(captured_path));
}

TEST(PlaybackCommandDispatcher, TempFileCleanedOnPlaybackError) {
    FakeAudioConverter conv;
    conv.play_result = edge_tts::common::Result<void>::fail(
        Error{ErrorCode::external_process_failed, "ffplay failed"});

    KnownTempProvider tp{"cleanup_play_err"};
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("audio")}};

    fs::path captured_path;
    auto provider = [&captured_path, &tp](std::string_view suffix)
            -> std::optional<fs::path> {
        if (suffix == ".srt") return std::nullopt;
        captured_path = tp.dir / ("known" + std::string(suffix));
        return captured_path;
    };

    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{make_factory(chunks), conv, std::move(provider),
                                 false, out, err, in};
    EXPECT_EQ(d.dispatch(make_play_result("hi")), 1);

    // Temp file must be deleted even when playback fails.
    EXPECT_FALSE(fs::exists(captured_path));
}

TEST(PlaybackCommandDispatcher, TempFileAbsentOnSynthesisError) {
    // When synthesis fails, save() never creates the file; dispatcher should
    // not leave an orphan file.
    FakeAudioConverter conv;
    KnownTempProvider  tp{"cleanup_synth_err"};

    fs::path captured_path;
    auto provider = [&captured_path, &tp](std::string_view suffix)
            -> std::optional<fs::path> {
        if (suffix == ".srt") return std::nullopt;
        captured_path = tp.dir / ("known" + std::string(suffix));
        return captured_path;
    };

    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{
        make_failing_factory(ErrorCode::network_error, "no connection"),
        conv, std::move(provider), false, out, err, in};

    EXPECT_EQ(d.dispatch(make_play_result("hi")), 1);
    // File was never created, cleanup guard's fs::remove is a no-op.
    EXPECT_FALSE(fs::exists(captured_path));
    EXPECT_FALSE(conv.play_called);
}

TEST(PlaybackCommandDispatcher, TempFileKeptWhenKeepTempTrue) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"keep_temp"};
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("audio")}};

    fs::path captured_path;
    auto provider = [&captured_path, &tp](std::string_view suffix)
            -> std::optional<fs::path> {
        if (suffix == ".srt") return std::nullopt;
        captured_path = tp.dir / ("known" + std::string(suffix));
        return captured_path;
    };

    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{make_factory(chunks), conv, std::move(provider),
                                 true /*keep_temp*/, out, err, in};
    EXPECT_EQ(d.dispatch(make_play_result("hi")), 0);

    // File must still exist when keep_temp is true.
    EXPECT_TRUE(fs::exists(captured_path));
    // Manual cleanup.
    fs::remove(captured_path);
}

// ---------------------------------------------------------------------------
// Dispatcher: error exit codes
// ---------------------------------------------------------------------------

TEST(PlaybackCommandDispatcher, SynthesisErrorReturns1) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"synth_err_code"};
    std::ostringstream out, err;
    std::istringstream in;

    PlaybackCommandDispatcher d{
        make_failing_factory(ErrorCode::service_error, "no audio"),
        conv, tp.provider(), false, out, err, in};

    EXPECT_EQ(d.dispatch(make_play_result("hi")), 1);
    EXPECT_FALSE(err.str().empty());
}

TEST(PlaybackCommandDispatcher, PlaybackErrorReturns1) {
    FakeAudioConverter conv;
    conv.play_result = edge_tts::common::Result<void>::fail(
        Error{ErrorCode::external_process_failed, "ffplay not found"});

    KnownTempProvider tp{"play_err_code"};
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("audio")}};
    std::ostringstream out, err;
    std::istringstream in;

    PlaybackCommandDispatcher d{make_factory(chunks), conv, tp.provider(),
                                 false, out, err, in};

    EXPECT_EQ(d.dispatch(make_play_result("hi")), 1);
    EXPECT_FALSE(err.str().empty());
}

// ---------------------------------------------------------------------------
// Dispatcher: playback receives the temp file path from synthesis
// ---------------------------------------------------------------------------

TEST(PlaybackCommandDispatcher, PlaybackReceivesCorrectTempPath) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"path_check"};
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("data")}};

    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{make_factory(chunks), conv, tp.provider_no_srt(),
                                 false, out, err, in};
    d.dispatch(make_play_result("hi"));

    // The path given to the player must end with ".mp3".
    EXPECT_EQ(conv.last_played.extension().string(), ".mp3");
}

// ---------------------------------------------------------------------------
// Dispatcher: proxy option is set in SynthesisOptions
// ---------------------------------------------------------------------------

TEST(PlaybackCommandDispatcher, ProxyOptionIsSetInSynthesisOptions) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"proxy_test"};
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("data")}};

    CapturingFactory cf;
    cf.chunks = chunks;

    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{cf.make(), conv, tp.provider_no_srt(),
                                 false, out, err, in};

    PlaybackParseResult r = make_play_result("hello");
    r.arguments.proxy = "http://proxy.example.com:8080";
    d.dispatch(r);

    ASSERT_TRUE(cf.last_options.proxy.has_value());
    EXPECT_EQ(*cf.last_options.proxy, "http://proxy.example.com:8080");
}

TEST(PlaybackCommandDispatcher, NoProxyLeavesOptionEmpty) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"no_proxy_test"};
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("data")}};

    CapturingFactory cf;
    cf.chunks = chunks;

    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{cf.make(), conv, tp.provider_no_srt(),
                                 false, out, err, in};
    d.dispatch(make_play_result("hello"));

    EXPECT_FALSE(cf.last_options.proxy.has_value());
}

// ---------------------------------------------------------------------------
// Dispatcher: missing player returns clear error naming the player
// ---------------------------------------------------------------------------

TEST(PlaybackCommandDispatcher, MissingPlayerErrorNamesPlayerInStderr) {
    // When ffplay is not found on PATH, FfmpegAudioConverter returns
    // "ffplay not found on PATH". Verify that message reaches stderr and names
    // the player so the user knows exactly what to install.
    FakeAudioConverter conv;
    conv.play_result = edge_tts::common::Result<void>::fail(
        Error{ErrorCode::external_process_failed, "ffplay not found on PATH"});
    KnownTempProvider tp{"missing_player"};
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("audio")}};

    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{make_factory(chunks), conv, tp.provider_no_srt(),
                                 false, out, err, in};

    int rc = d.dispatch(make_play_result("hello"));
    EXPECT_EQ(rc, 1);
    // Error message must name the missing executable so the user knows what to install.
    EXPECT_NE(err.str().find("ffplay"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Dispatcher: --mpv explicitly rejected
// ---------------------------------------------------------------------------

TEST(PlaybackCommandDispatcher, MpvFlagReturnsErrorWithClearMessage) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"mpv_reject"};
    std::ostringstream out, err;
    std::istringstream in;

    PlaybackCommandDispatcher d{make_factory({}), conv, tp.provider_no_srt(),
                                 false, out, err, in};

    PlaybackParseResult r = make_play_result("hello");
    r.arguments.use_mpv = true;
    int rc = d.dispatch(r);

    EXPECT_EQ(rc, 1);
    EXPECT_NE(err.str().find("--mpv"), std::string::npos);
    EXPECT_NE(err.str().find("ffplay"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Dispatcher: SRT temp file lifecycle
// ---------------------------------------------------------------------------

TEST(PlaybackCommandDispatcher, SrtTempFileCleanedOnSuccess) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"srt_cleanup_ok"};
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("audio")}};

    fs::path captured_mp3, captured_srt;
    auto provider = [&](std::string_view suffix) -> std::optional<fs::path> {
        if (suffix == ".mp3") {
            captured_mp3 = tp.dir / "test.mp3";
            return captured_mp3;
        }
        if (suffix == ".srt") {
            captured_srt = tp.dir / "test.srt";
            return captured_srt;
        }
        return std::nullopt;
    };

    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{make_factory(chunks), conv, std::move(provider),
                                 false, out, err, in};
    EXPECT_EQ(d.dispatch(make_play_result("hi")), 0);

    EXPECT_FALSE(fs::exists(captured_mp3));
    EXPECT_FALSE(fs::exists(captured_srt));
}

TEST(PlaybackCommandDispatcher, SrtTempFileKeptWhenKeepTempTrue) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"srt_keep_temp"};
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("audio")}};

    fs::path captured_mp3, captured_srt;
    auto provider = [&](std::string_view suffix) -> std::optional<fs::path> {
        if (suffix == ".mp3") {
            captured_mp3 = tp.dir / "test.mp3";
            return captured_mp3;
        }
        if (suffix == ".srt") {
            captured_srt = tp.dir / "test.srt";
            return captured_srt;
        }
        return std::nullopt;
    };

    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{make_factory(chunks), conv, std::move(provider),
                                 true /*keep_temp*/, out, err, in};
    EXPECT_EQ(d.dispatch(make_play_result("hi")), 0);

    // Both files must exist when keep_temp is true.
    EXPECT_TRUE(fs::exists(captured_mp3));
    EXPECT_TRUE(fs::exists(captured_srt));
    fs::remove(captured_mp3);
    fs::remove(captured_srt);
}

TEST(PlaybackCommandDispatcher, NoSrtWhenProviderReturnsNullopt) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"no_srt"};
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("audio")}};

    // Provider returns nullopt for ".srt" — synthesis is called without SRT.
    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{make_factory(chunks), conv, tp.provider_no_srt(),
                                 false, out, err, in};
    EXPECT_EQ(d.dispatch(make_play_result("hi")), 0);
    EXPECT_TRUE(conv.play_called);
}

// ---------------------------------------------------------------------------
// Dispatcher: EDGE_PLAYBACK_MP3_FILE honored via TempFileProvider
// (The env var is read in main.cpp's lambda; this test verifies the dispatcher
// respects whatever path the provider returns, including a user-supplied one.)
// ---------------------------------------------------------------------------

TEST(PlaybackCommandDispatcher, CustomMp3PathFromProviderIsUsed) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"custom_mp3"};
    std::vector<TtsChunk> chunks{TtsChunk{make_audio("audio")}};

    const fs::path custom_path = tp.dir / "custom.mp3";
    auto provider = [&custom_path](std::string_view suffix)
            -> std::optional<fs::path> {
        if (suffix == ".mp3") return custom_path;
        return std::nullopt;
    };

    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{make_factory(chunks), conv, std::move(provider),
                                 true /*keep_temp to inspect path*/, out, err, in};
    EXPECT_EQ(d.dispatch(make_play_result("hi")), 0);

    EXPECT_EQ(conv.last_played, custom_path);
    fs::remove(custom_path);
}

// ---------------------------------------------------------------------------
// Parser: rate, volume, pitch, proxy options
// ---------------------------------------------------------------------------

TEST(PlaybackArgumentParser, RateOption) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--rate", "+20%"});
    EXPECT_EQ(r.action, PlaybackParseAction::play);
    EXPECT_EQ(r.arguments.rate, "+20%");
}

TEST(PlaybackArgumentParser, VolumeOption) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--volume", "+10%"});
    EXPECT_EQ(r.action, PlaybackParseAction::play);
    EXPECT_EQ(r.arguments.volume, "+10%");
}

TEST(PlaybackArgumentParser, VolumeEqualsNegative) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--volume=-10%"});
    EXPECT_EQ(r.action, PlaybackParseAction::play);
    EXPECT_EQ(r.arguments.volume, "-10%");
}

TEST(PlaybackArgumentParser, PitchOption) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--pitch", "+5Hz"});
    EXPECT_EQ(r.action, PlaybackParseAction::play);
    EXPECT_EQ(r.arguments.pitch, "+5Hz");
}

TEST(PlaybackArgumentParser, ProxyOption) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--proxy", "http://proxy.example.com:8080"});
    EXPECT_EQ(r.action, PlaybackParseAction::play);
    ASSERT_TRUE(r.arguments.proxy.has_value());
    EXPECT_EQ(*r.arguments.proxy, "http://proxy.example.com:8080");
}

TEST(PlaybackArgumentParser, ProxyEmptyStringIsError) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--proxy", ""});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(PlaybackArgumentParser, ProxyMissingSchemeIsError) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--proxy", "barehost:8080"});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

// ---------------------------------------------------------------------------
// Parser: missing-value errors
// ---------------------------------------------------------------------------

TEST(PlaybackArgumentParser, VoiceMissingValueIsError) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--voice"});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
    EXPECT_NE(r.message.find("--voice"), std::string::npos);
}

TEST(PlaybackArgumentParser, RateMissingValueIsError) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--rate"});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
    EXPECT_NE(r.message.find("--rate"), std::string::npos);
}

TEST(PlaybackArgumentParser, VolumeMissingValueIsError) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--volume"});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
    EXPECT_NE(r.message.find("--volume"), std::string::npos);
}

TEST(PlaybackArgumentParser, PitchMissingValueIsError) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--pitch"});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
    EXPECT_NE(r.message.find("--pitch"), std::string::npos);
}

TEST(PlaybackArgumentParser, ProxyMissingValueIsError) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--proxy"});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
    EXPECT_NE(r.message.find("--proxy"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Parser: unknown and positional rejection
// ---------------------------------------------------------------------------

TEST(PlaybackArgumentParser, PositionalArgumentIsError) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"sometext"});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(PlaybackArgumentParser, UnknownLongOptionIsError) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--unknown-flag"});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

// ---------------------------------------------------------------------------
// Parser: defaults
// ---------------------------------------------------------------------------

TEST(PlaybackArgumentParser, DefaultVoice) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi"});
    EXPECT_EQ(r.arguments.voice, PlaybackArguments::kDefaultVoice);
}

TEST(PlaybackArgumentParser, DefaultRate) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi"});
    EXPECT_EQ(r.arguments.rate, "+0%");
}

TEST(PlaybackArgumentParser, DefaultVolume) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi"});
    EXPECT_EQ(r.arguments.volume, "+0%");
}

TEST(PlaybackArgumentParser, DefaultPitch) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi"});
    EXPECT_EQ(r.arguments.pitch, "+0Hz");
}

TEST(PlaybackArgumentParser, DefaultProxyIsAbsent) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi"});
    EXPECT_FALSE(r.arguments.proxy.has_value());
}

// ---------------------------------------------------------------------------
// Parser: help text completeness
// ---------------------------------------------------------------------------

TEST(PlaybackArgumentParser, HelpTextContainsAllDocumentedOptions) {
    PlaybackArgumentParser parser;
    const std::string h = parser.help_text();
    EXPECT_NE(h.find("--text"),   std::string::npos);
    EXPECT_NE(h.find("--file"),   std::string::npos);
    EXPECT_NE(h.find("--voice"),  std::string::npos);
    EXPECT_NE(h.find("--rate"),   std::string::npos);
    EXPECT_NE(h.find("--volume"), std::string::npos);
    EXPECT_NE(h.find("--pitch"),  std::string::npos);
    EXPECT_NE(h.find("--proxy"),  std::string::npos);
    EXPECT_NE(h.find("--mpv"),    std::string::npos);
    EXPECT_NE(h.find("--help"),   std::string::npos);
}

// ---------------------------------------------------------------------------
// Dispatcher: TTS config forwarding
// ---------------------------------------------------------------------------

namespace {

// Helper: build ParseResult with given field modifications.
static PlaybackParseResult make_play_with_voice(std::string voice) {
    auto r = make_play_result("hi");
    r.arguments.voice = std::move(voice);
    return r;
}

} // anonymous namespace

TEST(PlaybackCommandDispatcher, VoiceForwardedToFactory) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"fwd_voice"};
    TtsConfig received;
    auto factory = [&received](std::string text, TtsConfig cfg, SynthesisOptions) {
        received = cfg;
        return SpeechSynthesizer(std::move(text), std::move(cfg),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };
    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{factory, conv, tp.provider_no_srt(), false, out, err, in};
    d.dispatch(make_play_with_voice("en-GB-RyanNeural"));
    EXPECT_EQ(received.voice, "en-GB-RyanNeural");
}

TEST(PlaybackCommandDispatcher, RateForwardedToFactory) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"fwd_rate"};
    TtsConfig received;
    auto factory = [&received](std::string text, TtsConfig cfg, SynthesisOptions) {
        received = cfg;
        return SpeechSynthesizer(std::move(text), std::move(cfg),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };
    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{factory, conv, tp.provider_no_srt(), false, out, err, in};
    auto r = make_play_result("hi");
    r.arguments.rate = "+30%";
    d.dispatch(r);
    EXPECT_EQ(received.rate, "+30%");
}

TEST(PlaybackCommandDispatcher, VolumeForwardedToFactory) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"fwd_vol"};
    TtsConfig received;
    auto factory = [&received](std::string text, TtsConfig cfg, SynthesisOptions) {
        received = cfg;
        return SpeechSynthesizer(std::move(text), std::move(cfg),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };
    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{factory, conv, tp.provider_no_srt(), false, out, err, in};
    auto r = make_play_result("hi");
    r.arguments.volume = "-20%";
    d.dispatch(r);
    EXPECT_EQ(received.volume, "-20%");
}

TEST(PlaybackCommandDispatcher, PitchForwardedToFactory) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"fwd_pitch"};
    TtsConfig received;
    auto factory = [&received](std::string text, TtsConfig cfg, SynthesisOptions) {
        received = cfg;
        return SpeechSynthesizer(std::move(text), std::move(cfg),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };
    std::ostringstream out, err;
    std::istringstream in;
    PlaybackCommandDispatcher d{factory, conv, tp.provider_no_srt(), false, out, err, in};
    auto r = make_play_result("hi");
    r.arguments.pitch = "+10Hz";
    d.dispatch(r);
    EXPECT_EQ(received.pitch, "+10Hz");
}

// ---------------------------------------------------------------------------
// Dispatcher: stdin reading via --file=-
// ---------------------------------------------------------------------------

TEST(PlaybackCommandDispatcher, FileDashReadsFromStdin) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"stdin_read"};
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
    std::istringstream in("hello from stdin");
    PlaybackCommandDispatcher d{factory, conv, tp.provider_no_srt(), false, out, err, in};

    PlaybackParseResult r;
    r.action    = PlaybackParseAction::play;
    r.exit_code = 0;
    r.arguments.file = "-";

    int rc = d.dispatch(r);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(received_text, "hello from stdin");
}

// ---------------------------------------------------------------------------
// Parser: negative-value-with-space is a parse error
//
// `--rate -50%` is misinterpreted because `-50%` looks like an option token.
// Users must use `--rate=-50%` (equals form). Exit code 2.
// ---------------------------------------------------------------------------

TEST(PlaybackArgumentParser, RateNegativeWithSpaceIsError) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--rate", "-50%"});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(PlaybackArgumentParser, VolumeNegativeWithSpaceIsError) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--volume", "-10%"});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(PlaybackArgumentParser, PitchNegativeWithSpaceIsError) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--pitch", "-5Hz"});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

// ---------------------------------------------------------------------------
// Parser: negative-value equals form succeeds
// ---------------------------------------------------------------------------

TEST(PlaybackArgumentParser, RateEqualsNegative) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--rate=-50%"});
    EXPECT_EQ(r.action, PlaybackParseAction::play);
    EXPECT_EQ(r.arguments.rate, "-50%");
}

TEST(PlaybackArgumentParser, PitchEqualsNegative) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "--pitch=-5Hz"});
    EXPECT_EQ(r.action, PlaybackParseAction::play);
    EXPECT_EQ(r.arguments.pitch, "-5Hz");
}

// ---------------------------------------------------------------------------
// Parser: short-form missing-value errors
// ---------------------------------------------------------------------------

TEST(PlaybackArgumentParser, ShortTextMissingValueIsError) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"-t"});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(PlaybackArgumentParser, ShortFileMissingValueIsError) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"-f"});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

TEST(PlaybackArgumentParser, ShortVoiceMissingValueIsError) {
    PlaybackArgumentParser parser;
    auto r = parser.parse({"--text", "hi", "-v"});
    EXPECT_EQ(r.action, PlaybackParseAction::error);
    EXPECT_EQ(r.exit_code, 2);
}

// ---------------------------------------------------------------------------
// Parser: help text documents negative-value syntax
//
// CLI_COMPATIBILITY.md behavioral note 8 (shared with edge-tts):
// users must be told that `--rate=-50%` is the required form.
// ---------------------------------------------------------------------------

TEST(PlaybackArgumentParser, HelpTextMentionsNegativeValueSyntax) {
    PlaybackArgumentParser parser;
    const std::string h = parser.help_text();
    EXPECT_NE(h.find("="),        std::string::npos);
    EXPECT_NE(h.find("negative"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Dispatcher: missing input file returns exit 1 with path in error message.
//
// InputLoader returns io_error with the path in the context field.
// PlaybackCommandDispatcher::format_error forwards Error::what(), which
// includes the context, so the path must appear in stderr.
// ---------------------------------------------------------------------------

TEST(PlaybackCommandDispatcher, FileSynthesisMissingFileReturnsError) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"pb_missing_file"};
    std::ostringstream out, err;
    std::istringstream in;

    PlaybackCommandDispatcher d{make_factory({}), conv, tp.provider_no_srt(),
                                 false, out, err, in};

    PlaybackParseResult r;
    r.action    = PlaybackParseAction::play;
    r.exit_code = 0;
    r.arguments.file = "/no/such/playback_input.txt";

    EXPECT_EQ(d.dispatch(r), 1);
    EXPECT_FALSE(err.str().empty());
}

TEST(PlaybackCommandDispatcher, FileSynthesisMissingFileErrorIncludesPath) {
    FakeAudioConverter conv;
    KnownTempProvider  tp{"pb_missing_file_path"};
    std::ostringstream out, err;
    std::istringstream in;

    PlaybackCommandDispatcher d{make_factory({}), conv, tp.provider_no_srt(),
                                 false, out, err, in};

    PlaybackParseResult r;
    r.action    = PlaybackParseAction::play;
    r.exit_code = 0;
    r.arguments.file = "/no/such/playback_unique_input.txt";

    d.dispatch(r);
    EXPECT_NE(err.str().find("playback_unique_input.txt"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Dispatcher: proxy credential redaction
//
// When synthesis fails and the error context contains a proxy URL with
// embedded credentials, PlaybackCommandDispatcher must not expose the raw
// password in stderr output.
// ---------------------------------------------------------------------------

TEST(PlaybackCommandDispatcher, ProxyCredentialNotExposedInStderr) {
    // When a proxy URL with embedded credentials is provided, credentials must
    // not appear in stderr.  Proxy is rejected at the API layer before the
    // synthesizer function runs, so only a "proxy is not supported" message
    // appears — no URL, no credentials.
    FakeAudioConverter conv;
    KnownTempProvider  tp{"pb_proxy_redact"};
    std::ostringstream out, err;
    std::istringstream in;

    auto factory = [](std::string text, TtsConfig cfg, SynthesisOptions opts) {
        return SpeechSynthesizer(std::move(text), std::move(cfg), std::move(opts),
            [](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok({});
            });
    };

    PlaybackCommandDispatcher d{factory, conv, tp.provider_no_srt(),
                                 false, out, err, in};

    PlaybackParseResult r = make_play_result("hello");
    r.arguments.proxy = "http://user:topsecret@proxy.internal:3128";
    d.dispatch(r);

    EXPECT_EQ(err.str().find("topsecret"), std::string::npos);
    EXPECT_NE(err.str().find("proxy"), std::string::npos);
}
