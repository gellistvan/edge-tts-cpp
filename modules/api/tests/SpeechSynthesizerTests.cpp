#include "api/SpeechSynthesizer.hpp"
#include "communication/ConnectionMetadata.hpp"
#include "communication/EdgeProtocol.hpp"
#include "common/Clock.hpp"
#include "common/Error.hpp"
#include "core/Chunk.hpp"
#include "core/TtsConfig.hpp"
#include "support/ChunkTestHelpers.hpp"
#include "vendor/minigtest/minigtest.hpp"
#include "ApiTestFixtures.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

using edge_tts::api::SpeechSynthesizer;
using edge_tts::api::SynthesizerFn;
using edge_tts::communication::ConnectionMetadata;
using edge_tts::communication::EdgeProtocol;
using edge_tts::common::Error;
using edge_tts::common::ErrorCode;
using edge_tts::common::FixedClock;
using edge_tts::core::AudioChunk;
using edge_tts::core::BoundaryChunk;
using edge_tts::core::BoundaryEventType;
using edge_tts::core::TtsChunk;
using edge_tts::core::TtsConfig;
using edge_tts::test::make_audio;
using edge_tts::test::make_boundary;
using edge_tts::test::make_fake;
using edge_tts::test::valid_config;

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Test fixtures and helpers
// ---------------------------------------------------------------------------

static TtsConfig invalid_config() {
    TtsConfig cfg = TtsConfig::defaults();
    cfg.rate = "bad_rate";
    return cfg;
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

using edge_tts::test::FileGuard;
using edge_tts::test::read_file;
using edge_tts::test::read_file_binary;

// ---------------------------------------------------------------------------
// Constructor / accessors
// ---------------------------------------------------------------------------

TEST(SpeechSynthesizer, ConstructorStoresText) {
    SpeechSynthesizer c("hello world", valid_config(), make_fake({}));
    EXPECT_EQ(c.text(), "hello world");
}

TEST(SpeechSynthesizer, ConstructorStoresConfig) {
    TtsConfig cfg = valid_config();
    cfg.voice = "en-GB-RyanNeural";
    SpeechSynthesizer c("hello", cfg, make_fake({}));
    EXPECT_EQ(c.config().voice, "en-GB-RyanNeural");
}

// ---------------------------------------------------------------------------
// Invalid config
// ---------------------------------------------------------------------------

TEST(SpeechSynthesizer, InvalidConfigReturnsErrorOnStream) {
    SpeechSynthesizer c("hello", invalid_config(), make_fake({}));
    auto r = c.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_argument);
}

TEST(SpeechSynthesizer, InvalidConfigReturnsErrorOnSave) {
    const fs::path p = tmp_path("inv_cfg_save.mp3");
    FileGuard g{p};
    SpeechSynthesizer c("hello", invalid_config(), make_fake({}));
    auto r = c.save(p);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_argument);
}

// ---------------------------------------------------------------------------
// Empty text
// ---------------------------------------------------------------------------

TEST(SpeechSynthesizer, EmptyTextReturnsEmptyChunks) {
    // Empty text produces no audio chunks.
    SpeechSynthesizer c("", valid_config(), make_fake({{make_audio("audio")}}));
    auto r = c.synthesize();
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r->empty());
}

TEST(SpeechSynthesizer, WhitespaceOnlyTextReturnsEmptyChunks) {
    SpeechSynthesizer c("   \n\t  ", valid_config(), make_fake({{make_audio("audio")}}));
    auto r = c.synthesize();
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(r->empty());
}

// ---------------------------------------------------------------------------
// Text chunking
// ---------------------------------------------------------------------------

TEST(SpeechSynthesizer, ShortTextProducesSingleChunk) {
    CapturingSynthesizer cap;
    SpeechSynthesizer c("Hello world.", valid_config(), cap.make());
    auto r = c.synthesize();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(cap.received_chunks.size(), 1u);
}

TEST(SpeechSynthesizer, LongTextIsChunked) {
    // Build text that exceeds the 4096-byte chunk limit so it must be split.
    std::string long_text(5000, 'A');
    CapturingSynthesizer cap;
    SpeechSynthesizer c(long_text, valid_config(), cap.make());
    auto r = c.synthesize();
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(cap.received_chunks.size() > 1u);
}

TEST(SpeechSynthesizer, ChunksAreXmlEscaped) {
    // Text containing XML-special chars must be escaped before reaching the synthesizer.
    CapturingSynthesizer cap;
    SpeechSynthesizer c("hello & world", valid_config(), cap.make());
    auto r = c.synthesize();
    EXPECT_TRUE(r.has_value());
    EXPECT_FALSE(cap.received_chunks.empty());
    // The chunk must contain &amp; not &
    const std::string& chunk = cap.received_chunks.front();
    EXPECT_NE(chunk.find("&amp;"), std::string::npos);
}

TEST(SpeechSynthesizer, PreEscapedChunksProduceNoDoubleEscapingInSsml) {
    // Full pipeline regression: SpeechSynthesizer → TextChunker → EdgeProtocol.
    // "Tom & Jerry" must appear as "&amp;" exactly once in the final SSML,
    // never as "&amp;amp;" (which would indicate double-escaping).
    CapturingSynthesizer cap;
    SpeechSynthesizer c("Tom & Jerry", valid_config(), cap.make());
    auto r = c.synthesize();
    EXPECT_TRUE(r.has_value());
    ASSERT_EQ(cap.received_chunks.size(), 1u);

    const std::string& escaped_chunk = cap.received_chunks.front();
    // The captured chunk must already be XML-escaped by TextChunker.
    EXPECT_NE(escaped_chunk.find("&amp;"), std::string::npos);
    EXPECT_EQ(escaped_chunk.find("& Jerry"), std::string::npos);

    // Simulate what SynthesisSession does: pass the pre-escaped chunk to
    // EdgeProtocol::build_ssml_frame.  The SSML body must NOT double-escape.
    FixedClock clock{std::chrono::system_clock::time_point{}};
    EdgeProtocol proto{clock};
    ConnectionMetadata meta;
    meta.connection_id = "00000000000000000000000000000000";
    meta.request_id    = "00000000000000000000000000000000";

    auto ssml_frame = proto.build_ssml_frame(valid_config(), escaped_chunk, meta);
    ASSERT_TRUE(ssml_frame.has_value());
    // "&amp;" must appear exactly once — no "&amp;amp;".
    EXPECT_NE(ssml_frame->find("&amp;"),     std::string::npos);
    EXPECT_EQ(ssml_frame->find("&amp;amp;"), std::string::npos);
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

TEST(SpeechSynthesizer, StreamReturnsAudioChunks) {
    std::vector<TtsChunk> fake{make_audio("mp3data")};
    SpeechSynthesizer c("hello", valid_config(), make_fake(fake));
    auto r = c.synthesize();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 1u);
    EXPECT_TRUE(edge_tts::core::is_audio(r->at(0)));
}

TEST(SpeechSynthesizer, StreamReturnsBoundaryChunks) {
    std::vector<TtsChunk> fake{
        make_audio("mp3data"),
        TtsChunk{make_boundary("hello")}};
    SpeechSynthesizer c("hello", valid_config(), make_fake(fake));
    auto r = c.synthesize();
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 2u);
    EXPECT_TRUE(edge_tts::core::is_audio(r->at(0)));
    EXPECT_TRUE(edge_tts::core::is_boundary(r->at(1)));
}

TEST(SpeechSynthesizer, StreamReturnsMixedChunksInOrder) {
    std::vector<TtsChunk> fake{
        TtsChunk{make_audio("first")},
        TtsChunk{make_boundary("word1", 0, 5'000'000)},
        TtsChunk{make_audio("second")},
        TtsChunk{make_boundary("word2", 5'000'000, 5'000'000)},
    };
    SpeechSynthesizer c("some text", valid_config(), make_fake(fake));
    auto r = c.synthesize();
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

TEST(SpeechSynthesizer, SaveWritesMediaBytesInOrder) {
    // Two audio chunks: bytes must be concatenated in arrival order.
    std::vector<TtsChunk> fake{
        TtsChunk{make_audio("PART1")},
        TtsChunk{make_audio("PART2")},
    };

    const fs::path mp = tmp_path("save_media.mp3");
    FileGuard gm{mp};

    SpeechSynthesizer c("hello", valid_config(), make_fake(fake));
    auto r = c.save(mp);
    EXPECT_TRUE(r.has_value());

    const auto bytes = read_file_binary(mp);
    const std::string content(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    EXPECT_EQ(content, "PART1PART2");
}

TEST(SpeechSynthesizer, SaveNoSubtitlePathSkipsSubtitleFile) {
    std::vector<TtsChunk> fake{TtsChunk{make_audio("audio")}};
    const fs::path mp = tmp_path("save_no_srt.mp3");
    FileGuard gm{mp};

    // No subtitle path; ensure no subtitle file is created.
    const fs::path srt_path = tmp_path("save_no_srt.srt");
    fs::remove(srt_path);

    SpeechSynthesizer c("hello", valid_config(), make_fake(fake));
    auto r = c.save(mp);
    EXPECT_TRUE(r.has_value());
    EXPECT_FALSE(fs::exists(srt_path));
}

// ---------------------------------------------------------------------------
// save() — subtitle file
// ---------------------------------------------------------------------------

TEST(SpeechSynthesizer, SaveWritesSubtitleFileWhenPathGiven) {
    // Feed a boundary chunk so SubMaker has something to write.
    std::vector<TtsChunk> fake{
        TtsChunk{make_audio("mp3bytes")},
        TtsChunk{make_boundary("Hello world", 0, 10'000'000)},
    };

    const fs::path mp  = tmp_path("save_srt.mp3");
    const fs::path srt = tmp_path("save_srt.srt");
    FileGuard gm{mp};
    FileGuard gs{srt};

    SpeechSynthesizer c("Hello world", valid_config(), make_fake(fake));
    auto r = c.save(mp, srt);
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(fs::exists(srt));

    const std::string content = read_file(srt);
    // SRT format: block number, timestamp line, text, blank line
    EXPECT_FALSE(content.empty());
    EXPECT_NE(content.find("-->"), std::string::npos);
}

TEST(SpeechSynthesizer, SaveWithNoBoundariesProducesEmptySrt) {
    // Audio only; SubMaker produces an empty (or minimal) SRT.
    std::vector<TtsChunk> fake{TtsChunk{make_audio("mp3bytes")}};

    const fs::path mp  = tmp_path("save_empty_srt.mp3");
    const fs::path srt = tmp_path("save_empty_srt.srt");
    FileGuard gm{mp};
    FileGuard gs{srt};

    SpeechSynthesizer c("Hello", valid_config(), make_fake(fake));
    auto r = c.save(mp, srt);
    EXPECT_TRUE(r.has_value());
    EXPECT_TRUE(fs::exists(srt));
}

TEST(SpeechSynthesizer, SubtitleTimingUnchangedByAudioByteSize) {
    // Regression guard: SubMaker derives cue times from BoundaryChunk::offset_ticks
    // and duration_ticks only.  Changing the audio payload size must not alter
    // SRT output when boundary metadata is identical.
    //
    // This covers the single-chunk case where offset_compensation is always 0
    // regardless of audio byte count (cumulative_audio_bytes is zero when chunk 1
    // starts).  For multi-chunk timing the SynthesisSession comment documents
    // the MP3/48kbps approximation used there.

    // 500ms start, 1000ms end → expected timestamp line.
    static constexpr std::int64_t TICKS_PER_MS = 10'000;
    const std::int64_t offset_ticks   = 500  * TICKS_PER_MS;
    const std::int64_t duration_ticks = 1000 * TICKS_PER_MS;

    auto make_chunks = [&](std::size_t n_audio_bytes) -> std::vector<TtsChunk> {
        AudioChunk ac;
        ac.data = std::vector<std::byte>(n_audio_bytes, std::byte{0xAB});
        return {TtsChunk{std::move(ac)},
                TtsChunk{make_boundary("Hello", offset_ticks, duration_ticks)}};
    };

    const fs::path mp1  = tmp_path("srt_bytes1.mp3");
    const fs::path srt1 = tmp_path("srt_bytes1.srt");
    const fs::path mp2  = tmp_path("srt_bytes2.mp3");
    const fs::path srt2 = tmp_path("srt_bytes2.srt");
    FileGuard g1m{mp1}; FileGuard g1s{srt1};
    FileGuard g2m{mp2}; FileGuard g2s{srt2};

    SpeechSynthesizer a("Hello", valid_config(), make_fake(make_chunks(100)));
    EXPECT_TRUE(a.save(mp1, srt1).has_value());

    SpeechSynthesizer b("Hello", valid_config(), make_fake(make_chunks(10'000)));
    EXPECT_TRUE(b.save(mp2, srt2).has_value());

    const std::string srt_a = read_file(srt1);
    const std::string srt_b = read_file(srt2);

    // Bit-for-bit identical SRT regardless of audio payload size.
    EXPECT_EQ(srt_a, srt_b);
    // Sanity: the expected timestamps are present.
    EXPECT_NE(srt_a.find("00:00:00,500 --> 00:00:01,500"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Error propagation
// ---------------------------------------------------------------------------

TEST(SpeechSynthesizer, SessionErrorPropagatesFromStream) {
    SpeechSynthesizer c("hello", valid_config(),
                  make_failing(ErrorCode::network_error, "connection refused"));
    auto r = c.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::network_error);
}

TEST(SpeechSynthesizer, SessionErrorPropagatesFromSave) {
    const fs::path mp = tmp_path("sess_err_save.mp3");
    SpeechSynthesizer c("hello", valid_config(),
                  make_failing(ErrorCode::service_error, "no audio received"));
    auto r = c.save(mp);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::service_error);
}

TEST(SpeechSynthesizer, FileErrorPropagatesFromSave) {
    std::vector<TtsChunk> fake{TtsChunk{make_audio("mp3bytes")}};
    // Write to a path whose parent directory does not exist.
    const fs::path bad_mp = tmp_path("no_such_dir/comm.mp3");
    SpeechSynthesizer c("hello", valid_config(), make_fake(fake));
    auto r = c.save(bad_mp);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::io_error);
}

TEST(SpeechSynthesizer, SubtitleFileErrorPropagatesFromSave) {
    std::vector<TtsChunk> fake{TtsChunk{make_audio("mp3bytes")}};
    const fs::path mp     = tmp_path("srt_err.mp3");
    const fs::path bad_srt = tmp_path("no_such_dir/sub.srt");
    FileGuard gm{mp};

    SpeechSynthesizer c("hello", valid_config(), make_fake(fake));
    auto r = c.save(mp, bad_srt);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::io_error);
}

// ---------------------------------------------------------------------------
// Single-use stream behavior
// ---------------------------------------------------------------------------

TEST(SpeechSynthesizer, SynthesizeIsSingleUse) {
    // synthesize() is single-use.
    SpeechSynthesizer c("hello", valid_config(), make_fake({{make_audio("mp3")}}));
    auto r1 = c.synthesize();
    EXPECT_TRUE(r1.has_value());

    auto r2 = c.synthesize();
    EXPECT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().code(), ErrorCode::invalid_state);
}

TEST(SpeechSynthesizer, SaveIsSingleUse) {
    std::vector<TtsChunk> fake{TtsChunk{make_audio("mp3")}};
    const fs::path mp1 = tmp_path("single_use1.mp3");
    const fs::path mp2 = tmp_path("single_use2.mp3");
    FileGuard g1{mp1};

    SpeechSynthesizer c("hello", valid_config(), make_fake(fake));
    auto r1 = c.save(mp1);
    EXPECT_TRUE(r1.has_value());

    auto r2 = c.save(mp2);
    EXPECT_FALSE(r2.has_value());
    EXPECT_EQ(r2.error().code(), ErrorCode::invalid_state);
}

TEST(SpeechSynthesizer, SaveThenStreamIsSingleUse) {
    std::vector<TtsChunk> fake{TtsChunk{make_audio("mp3")}};
    const fs::path mp = tmp_path("save_then_stream.mp3");
    FileGuard g{mp};

    SpeechSynthesizer c("hello", valid_config(), make_fake(fake));
    (void)c.save(mp);

    auto r = c.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::invalid_state);
}

// ---------------------------------------------------------------------------
// Error taxonomy — one test per ErrorCode to verify transparent propagation.
// See docs/MODULES.md for the full consumer-facing error taxonomy table.
// ---------------------------------------------------------------------------

TEST(SpeechSynthesizer, ProtocolErrorPropagatesFromStream) {
    SpeechSynthesizer c("hello", valid_config(),
        make_failing(ErrorCode::protocol_error, "unexpected turn.start"));
    auto r = c.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::protocol_error);
}

TEST(SpeechSynthesizer, ParseErrorPropagatesFromStream) {
    SpeechSynthesizer c("hello", valid_config(),
        make_failing(ErrorCode::parse_error, "invalid UTF-8 in metadata JSON"));
    auto r = c.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

TEST(SpeechSynthesizer, TimeoutErrorPropagatesFromStream) {
    SpeechSynthesizer c("hello", valid_config(),
        make_failing(ErrorCode::timeout, "receive timeout after 60 s"));
    auto r = c.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::timeout);
}

TEST(SpeechSynthesizer, DrmErrorPropagatesFromStream) {
    SpeechSynthesizer c("hello", valid_config(),
        make_failing(ErrorCode::drm_error, "HTTP 403 after clock-skew retry"));
    auto r = c.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::drm_error);
}

TEST(SpeechSynthesizer, UnsupportedErrorPropagatesFromStream) {
    SpeechSynthesizer c("hello", valid_config(),
        make_failing(ErrorCode::unsupported, "proxy not implemented"));
    auto r = c.synthesize();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::unsupported);
}

TEST(SpeechSynthesizer, ProtocolErrorPropagatesFromSave) {
    const fs::path mp = tmp_path("proto_err_save.mp3");
    SpeechSynthesizer c("hello", valid_config(),
        make_failing(ErrorCode::protocol_error, "unknown binary path"));
    auto r = c.save(mp);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::protocol_error);
}

TEST(SpeechSynthesizer, ParseErrorPropagatesFromSave) {
    const fs::path mp = tmp_path("parse_err_save.mp3");
    SpeechSynthesizer c("hello", valid_config(),
        make_failing(ErrorCode::parse_error, "bad JSON root"));
    auto r = c.save(mp);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::parse_error);
}

TEST(SpeechSynthesizer, DrmErrorPropagatesFromSave) {
    const fs::path mp = tmp_path("drm_err_save.mp3");
    SpeechSynthesizer c("hello", valid_config(),
        make_failing(ErrorCode::drm_error, "403 DRM rejected after retry"));
    auto r = c.save(mp);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::drm_error);
}
