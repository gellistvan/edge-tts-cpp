#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/common/Error.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

using edge_tts::api::Communicate;
using edge_tts::api::SynthesizerFn;
using edge_tts::common::Error;
using edge_tts::common::ErrorCode;
using edge_tts::core::AudioChunk;
using edge_tts::core::BoundaryChunk;
using edge_tts::core::BoundaryEventType;
using edge_tts::core::TtsChunk;
using edge_tts::core::TtsConfig;

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Test fixtures and helpers
// ---------------------------------------------------------------------------

static TtsConfig valid_config() { return TtsConfig::defaults(); }

static TtsConfig invalid_config() {
    TtsConfig cfg = TtsConfig::defaults();
    cfg.rate = "bad_rate";
    return cfg;
}

static AudioChunk make_audio(std::string_view s) {
    AudioChunk ac;
    ac.data.reserve(s.size());
    for (char c : s)
        ac.data.push_back(static_cast<std::byte>(c));
    return ac;
}

static BoundaryChunk make_boundary(std::string text,
                                   std::int64_t offset_ticks = 0,
                                   std::int64_t duration_ticks = 10'000'000) {
    BoundaryChunk bc;
    bc.type           = BoundaryEventType::SentenceBoundary;
    bc.text           = std::move(text);
    bc.offset_ticks   = offset_ticks;
    bc.duration_ticks = duration_ticks;
    return bc;
}

static SynthesizerFn make_fake(std::vector<TtsChunk> chunks) {
    return [chunks = std::move(chunks)](
               const TtsConfig&,
               std::span<const std::string>)
               -> edge_tts::common::Result<std::vector<TtsChunk>> {
        return edge_tts::common::Result<std::vector<TtsChunk>>::ok(chunks);
    };
}

static SynthesizerFn make_failing(ErrorCode code, std::string msg) {
    return [code, msg = std::move(msg)](
               const TtsConfig&,
               std::span<const std::string>)
               -> edge_tts::common::Result<std::vector<TtsChunk>> {
        return edge_tts::common::Result<std::vector<TtsChunk>>::fail(
            Error{code, msg});
    };
}

// Returns the chunks received by the synthesizer so tests can inspect them.
struct CapturingSynthesizer {
    std::vector<std::string> received_chunks;

    SynthesizerFn make(std::vector<TtsChunk> response = {}) {
        return [this, response = std::move(response)](
                   const TtsConfig&,
                   std::span<const std::string> chunks)
                   -> edge_tts::common::Result<std::vector<TtsChunk>> {
            received_chunks.assign(chunks.begin(), chunks.end());
            return edge_tts::common::Result<std::vector<TtsChunk>>::ok(response);
        };
    }
};

static fs::path tmp_path(const std::string& name) {
    return fs::temp_directory_path() / ("edge_tts_comm_test_" + name);
}

static std::vector<std::byte> read_binary(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    const std::vector<char> buf{std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>{}};
    std::vector<std::byte> out(buf.size());
    for (std::size_t i = 0; i < buf.size(); ++i)
        out[i] = static_cast<std::byte>(buf[i]);
    return out;
}

static std::string read_text(const fs::path& p) {
    std::ifstream f(p);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>{}};
}

struct FileGuard {
    fs::path path;
    ~FileGuard() { fs::remove(path); }
};

// ---------------------------------------------------------------------------
// Constructor / accessors
// ---------------------------------------------------------------------------

TEST(Communicate, ConstructorStoresText) {
    Communicate c("hello world", valid_config(), make_fake({}));
    EXPECT_EQ(c.text(), "hello world");
}

TEST(Communicate, ConstructorStoresConfig) {
    TtsConfig cfg = valid_config();
    cfg.voice = "en-GB-RyanNeural";
    Communicate c("hello", cfg, make_fake({}));
    EXPECT_EQ(c.config().voice, "en-GB-RyanNeural");
}

// ---------------------------------------------------------------------------
// Invalid config
// ---------------------------------------------------------------------------

TEST(Communicate, InvalidConfigReturnsErrorOnStream) {
    Communicate c("hello", invalid_config(), make_fake({}));
    auto r = c.stream_sync();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_argument);
}

TEST(Communicate, InvalidConfigReturnsErrorOnSave) {
    const fs::path p = tmp_path("inv_cfg_save.mp3");
    FileGuard g{p};
    Communicate c("hello", invalid_config(), make_fake({}));
    auto r = c.save(p);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_argument);
}

// ---------------------------------------------------------------------------
// Empty text
// ---------------------------------------------------------------------------

TEST(Communicate, EmptyTextReturnsEmptyChunks) {
    // Reference: empty texts generator → stream yields nothing.
    Communicate c("", valid_config(), make_fake({{make_audio("audio")}}));
    auto r = c.stream_sync();
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r->empty());
}

TEST(Communicate, WhitespaceOnlyTextReturnsEmptyChunks) {
    Communicate c("   \n\t  ", valid_config(), make_fake({{make_audio("audio")}}));
    auto r = c.stream_sync();
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r->empty());
}

// ---------------------------------------------------------------------------
// Text chunking
// ---------------------------------------------------------------------------

TEST(Communicate, ShortTextProducesSingleChunk) {
    CapturingSynthesizer cap;
    Communicate c("Hello world.", valid_config(), cap.make());
    auto r = c.stream_sync();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(cap.received_chunks.size(), 1u);
}

TEST(Communicate, LongTextIsChunked) {
    // Build text that exceeds the 4096-byte chunk limit so it must be split.
    std::string long_text(5000, 'A');
    CapturingSynthesizer cap;
    Communicate c(long_text, valid_config(), cap.make());
    auto r = c.stream_sync();
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(cap.received_chunks.size() > 1u);
}

TEST(Communicate, ChunksAreXmlEscaped) {
    // Text containing XML-special chars must be escaped before reaching the synthesizer.
    CapturingSynthesizer cap;
    Communicate c("hello & world", valid_config(), cap.make());
    auto r = c.stream_sync();
    EXPECT_TRUE(r.has_value());
    EXPECT_FALSE(cap.received_chunks.empty());
    // The chunk must contain &amp; not &
    const std::string& chunk = cap.received_chunks.front();
    EXPECT_NE(chunk.find("&amp;"), std::string::npos);
}

// ---------------------------------------------------------------------------
// stream_sync returns audio and boundary chunks
// ---------------------------------------------------------------------------

TEST(Communicate, StreamReturnsAudioChunks) {
    std::vector<TtsChunk> fake{make_audio("mp3data")};
    Communicate c("hello", valid_config(), make_fake(fake));
    auto r = c.stream_sync();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 1u);
    EXPECT_TRUE(edge_tts::core::is_audio(r->at(0)));
}

TEST(Communicate, StreamReturnsBoundaryChunks) {
    std::vector<TtsChunk> fake{
        make_audio("mp3data"),
        TtsChunk{make_boundary("hello")}};
    Communicate c("hello", valid_config(), make_fake(fake));
    auto r = c.stream_sync();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 2u);
    EXPECT_TRUE(edge_tts::core::is_audio(r->at(0)));
    EXPECT_TRUE(edge_tts::core::is_boundary(r->at(1)));
}

TEST(Communicate, StreamReturnsMixedChunksInOrder) {
    std::vector<TtsChunk> fake{
        TtsChunk{make_audio("first")},
        TtsChunk{make_boundary("word1", 0, 5'000'000)},
        TtsChunk{make_audio("second")},
        TtsChunk{make_boundary("word2", 5'000'000, 5'000'000)},
    };
    Communicate c("some text", valid_config(), make_fake(fake));
    auto r = c.stream_sync();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 4u);
    EXPECT_TRUE(edge_tts::core::is_audio(r->at(0)));
    EXPECT_TRUE(edge_tts::core::is_boundary(r->at(1)));
    EXPECT_TRUE(edge_tts::core::is_audio(r->at(2)));
    EXPECT_TRUE(edge_tts::core::is_boundary(r->at(3)));
}

// ---------------------------------------------------------------------------
// save() — media file
// ---------------------------------------------------------------------------

TEST(Communicate, SaveWritesMediaBytesInOrder) {
    // Two audio chunks: bytes must be concatenated in arrival order.
    std::vector<TtsChunk> fake{
        TtsChunk{make_audio("PART1")},
        TtsChunk{make_audio("PART2")},
    };

    const fs::path mp = tmp_path("save_media.mp3");
    FileGuard gm{mp};

    Communicate c("hello", valid_config(), make_fake(fake));
    auto r = c.save(mp);
    EXPECT_TRUE(r.has_value());

    const auto bytes = read_binary(mp);
    const std::string content(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    EXPECT_EQ(content, "PART1PART2");
}

TEST(Communicate, SaveNoSubtitlePathSkipsSubtitleFile) {
    std::vector<TtsChunk> fake{TtsChunk{make_audio("audio")}};
    const fs::path mp = tmp_path("save_no_srt.mp3");
    FileGuard gm{mp};

    // No subtitle path; ensure no subtitle file is created.
    const fs::path srt_path = tmp_path("save_no_srt.srt");
    fs::remove(srt_path);

    Communicate c("hello", valid_config(), make_fake(fake));
    auto r = c.save(mp);
    EXPECT_TRUE(r.has_value());
    EXPECT_FALSE(fs::exists(srt_path));
}

// ---------------------------------------------------------------------------
// save() — subtitle file
// ---------------------------------------------------------------------------

TEST(Communicate, SaveWritesSubtitleFileWhenPathGiven) {
    // Feed a boundary chunk so SubMaker has something to write.
    std::vector<TtsChunk> fake{
        TtsChunk{make_audio("mp3bytes")},
        TtsChunk{make_boundary("Hello world", 0, 10'000'000)},
    };

    const fs::path mp  = tmp_path("save_srt.mp3");
    const fs::path srt = tmp_path("save_srt.srt");
    FileGuard gm{mp};
    FileGuard gs{srt};

    Communicate c("Hello world", valid_config(), make_fake(fake));
    auto r = c.save(mp, srt);
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(fs::exists(srt));

    const std::string content = read_text(srt);
    // SRT format: block number, timestamp line, text, blank line
    EXPECT_FALSE(content.empty());
    EXPECT_NE(content.find("-->"), std::string::npos);
}

TEST(Communicate, SaveWithNoBoundariesProducesEmptySrt) {
    // Audio only; SubMaker produces an empty (or minimal) SRT.
    std::vector<TtsChunk> fake{TtsChunk{make_audio("mp3bytes")}};

    const fs::path mp  = tmp_path("save_empty_srt.mp3");
    const fs::path srt = tmp_path("save_empty_srt.srt");
    FileGuard gm{mp};
    FileGuard gs{srt};

    Communicate c("Hello", valid_config(), make_fake(fake));
    auto r = c.save(mp, srt);
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(fs::exists(srt));
}

// ---------------------------------------------------------------------------
// Error propagation
// ---------------------------------------------------------------------------

TEST(Communicate, SessionErrorPropagatesFromStream) {
    Communicate c("hello", valid_config(),
                  make_failing(ErrorCode::network_error, "connection refused"));
    auto r = c.stream_sync();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::network_error);
}

TEST(Communicate, SessionErrorPropagatesFromSave) {
    const fs::path mp = tmp_path("sess_err_save.mp3");
    Communicate c("hello", valid_config(),
                  make_failing(ErrorCode::service_error, "no audio received"));
    auto r = c.save(mp);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::service_error);
}

TEST(Communicate, FileErrorPropagatesFromSave) {
    std::vector<TtsChunk> fake{TtsChunk{make_audio("mp3bytes")}};
    // Write to a path whose parent directory does not exist.
    const fs::path bad_mp = tmp_path("no_such_dir/comm.mp3");
    Communicate c("hello", valid_config(), make_fake(fake));
    auto r = c.save(bad_mp);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::io_error);
}

TEST(Communicate, SubtitleFileErrorPropagatesFromSave) {
    std::vector<TtsChunk> fake{TtsChunk{make_audio("mp3bytes")}};
    const fs::path mp     = tmp_path("srt_err.mp3");
    const fs::path bad_srt = tmp_path("no_such_dir/sub.srt");
    FileGuard gm{mp};

    Communicate c("hello", valid_config(), make_fake(fake));
    auto r = c.save(mp, bad_srt);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::io_error);
}

// ---------------------------------------------------------------------------
// Single-use stream behavior
// ---------------------------------------------------------------------------

TEST(Communicate, StreamSyncIsSingleUse) {
    // Reference: Communicate.stream() raises RuntimeError on second call.
    Communicate c("hello", valid_config(), make_fake({{make_audio("mp3")}}));
    auto r1 = c.stream_sync();
    EXPECT_TRUE(r1.has_value());

    auto r2 = c.stream_sync();
    EXPECT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().code(), ErrorCode::invalid_state);
}

TEST(Communicate, SaveIsSingleUse) {
    std::vector<TtsChunk> fake{TtsChunk{make_audio("mp3")}};
    const fs::path mp1 = tmp_path("single_use1.mp3");
    const fs::path mp2 = tmp_path("single_use2.mp3");
    FileGuard g1{mp1};

    Communicate c("hello", valid_config(), make_fake(fake));
    auto r1 = c.save(mp1);
    EXPECT_TRUE(r1.has_value());

    auto r2 = c.save(mp2);
    EXPECT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().code(), ErrorCode::invalid_state);
}

TEST(Communicate, SaveThenStreamIsSingleUse) {
    std::vector<TtsChunk> fake{TtsChunk{make_audio("mp3")}};
    const fs::path mp = tmp_path("save_then_stream.mp3");
    FileGuard g{mp};

    Communicate c("hello", valid_config(), make_fake(fake));
    (void)c.save(mp);

    auto r = c.stream_sync();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_state);
}
