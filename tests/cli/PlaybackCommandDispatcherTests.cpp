#include "edge_tts/cli/PlaybackArguments.hpp"
#include "edge_tts/cli/PlaybackCommandDispatcher.hpp"
#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/common/Error.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "edge_tts/media/AudioConverter.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using edge_tts::api::Communicate;
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

// Build AudioChunk bytes from string.
static AudioChunk make_audio(std::string_view s) {
    AudioChunk ac;
    ac.data.reserve(s.size());
    for (char c : s) ac.data.push_back(static_cast<std::byte>(c));
    return ac;
}

// CommunicateFactory that returns audio chunks.
static PlaybackCommandDispatcher::CommunicateFactory
make_factory(std::vector<TtsChunk> chunks) {
    return [chunks = std::move(chunks)](std::string text, TtsConfig cfg) {
        return Communicate(std::move(text), std::move(cfg),
            [chunks](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::ok(chunks);
            });
    };
}

// CommunicateFactory that always fails.
static PlaybackCommandDispatcher::CommunicateFactory
make_failing_factory(ErrorCode code, std::string msg) {
    return [code, msg = std::move(msg)](std::string text, TtsConfig cfg) {
        return Communicate(std::move(text), std::move(cfg),
            [code, msg](const TtsConfig&, std::span<const std::string>)
                -> edge_tts::common::Result<std::vector<TtsChunk>> {
                return edge_tts::common::Result<std::vector<TtsChunk>>::fail(
                    Error{code, msg});
            });
    };
}

// TempFileProvider that returns a known path (and creates the directory).
struct KnownTempProvider {
    fs::path dir;
    int      counter{0};

    explicit KnownTempProvider(const std::string& tag)
        : dir(fs::temp_directory_path() / ("pcd_test_" + tag)) {
        fs::create_directories(dir);
    }
    ~KnownTempProvider() { fs::remove_all(dir); }

    PlaybackCommandDispatcher::TempFileProvider provider() {
        return [this](std::string_view suffix) -> fs::path {
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
    // Reference: --list-voices is not supported by edge-playback.
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
    auto factory = [&received_text](std::string text, TtsConfig cfg) {
        received_text = text;
        return Communicate(std::move(text), std::move(cfg),
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
    auto provider = [&captured_path, &tp](std::string_view suffix) -> fs::path {
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
    auto provider = [&captured_path, &tp](std::string_view suffix) -> fs::path {
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
    auto provider = [&captured_path, &tp](std::string_view suffix) -> fs::path {
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
    auto provider = [&captured_path, &tp](std::string_view suffix) -> fs::path {
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
    PlaybackCommandDispatcher d{make_factory(chunks), conv, tp.provider(),
                                 false, out, err, in};
    d.dispatch(make_play_result("hi"));

    // The path given to the player must end with ".mp3".
    EXPECT_EQ(conv.last_played.extension().string(), ".mp3");
}
