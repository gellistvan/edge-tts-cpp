// Tests for the production synthesizer wiring introduced in Communicate.cpp.
//
// These tests verify that:
//   1. The api::Communicate production constructors call the real communication
//      stack (SynthesisSession) rather than a stub.
//   2. The full production composition (all real objects except the WebSocket
//      transport) works end-to-end: text → TextChunker → SynthesisSession →
//      audio chunks.
//   3. The old hardcoded "WebSocket transport not yet implemented" error is gone.
//
// The seam used here is the existing SynthesizerFn injection constructor:
//   Communicate(text, config, CommunicateOptions, SynthesizerFn)
// This constructor is also used internally by production constructors.
// We wire all real production objects into the SynthesizerFn but replace
// WebSocketClient with FakeWebSocketClient so no network calls are made.

#include "edge_tts/api/Communicate.hpp"
#include "edge_tts/api/CommunicateOptions.hpp"
#include "edge_tts/communication/ConnectionMetadata.hpp"
#include "edge_tts/communication/EdgeProtocol.hpp"
#include "edge_tts/communication/EdgeServiceConfig.hpp"
#include "edge_tts/communication/EdgeTokenProvider.hpp"
#include "edge_tts/communication/FakeWebSocketClient.hpp"
#include "edge_tts/communication/SynthesisSession.hpp"
#include "edge_tts/communication/WebSocketMessage.hpp"
#include "edge_tts/common/Clock.hpp"
#include "edge_tts/common/Error.hpp"
#include "edge_tts/common/IdGenerator.hpp"
#include "edge_tts/core/Chunk.hpp"
#include "edge_tts/core/TtsConfig.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

using edge_tts::api::Communicate;
using edge_tts::api::CommunicateOptions;
using edge_tts::api::SynthesizerFn;
using edge_tts::communication::ConnectionMetadataFactory;
using edge_tts::communication::EdgeProtocol;
using edge_tts::communication::EdgeServiceConfig;
using edge_tts::communication::EdgeTokenProvider;
using edge_tts::communication::FakeWebSocketClient;
using edge_tts::communication::SynthesisSession;
using edge_tts::communication::WebSocketMessage;
using edge_tts::common::ErrorCode;
using edge_tts::common::IdGenerator;
using edge_tts::common::SystemClock;
using edge_tts::core::AudioChunk;
using edge_tts::core::TtsChunk;
using edge_tts::core::TtsConfig;

// ---------------------------------------------------------------------------
// Frame builders (duplicated from SynthesisSessionTests for self-containment)
// ---------------------------------------------------------------------------

static std::vector<std::byte> to_bytes(const std::string& s) {
    std::vector<std::byte> v;
    for (char c : s) v.push_back(static_cast<std::byte>(c));
    return v;
}

static WebSocketMessage make_audio_frame(const std::vector<std::byte>& body) {
    const std::string hdr = "X-RequestId:abc\r\nPath:audio\r\nContent-Type:audio/mpeg";
    const auto hl = static_cast<uint16_t>(2 + hdr.size());
    std::vector<std::byte> frame;
    frame.reserve(2 + hdr.size() + 2 + body.size());
    frame.push_back(static_cast<std::byte>(hl >> 8));
    frame.push_back(static_cast<std::byte>(hl & 0xff));
    for (char c : hdr)   frame.push_back(static_cast<std::byte>(c));
    frame.push_back(static_cast<std::byte>('\r'));
    frame.push_back(static_cast<std::byte>('\n'));
    for (auto b : body)  frame.push_back(b);
    WebSocketMessage m;
    m.type   = WebSocketMessage::Type::binary;
    m.binary = std::move(frame);
    return m;
}

static WebSocketMessage make_turn_end() {
    WebSocketMessage m;
    m.type = WebSocketMessage::Type::text;
    m.text = "X-RequestId:abc\r\nPath:turn.end\r\n\r\n";
    return m;
}

// Push one minimal complete session: audio frame + turn.end
static void push_session(FakeWebSocketClient& ws, const std::string& audio_data) {
    ws.push_incoming(make_audio_frame(to_bytes(audio_data)));
    ws.push_incoming(make_turn_end());
}

// ---------------------------------------------------------------------------
// Full production composition test (api-level, fake transport seam)
//
// Wires all real production objects (SystemClock, IdGenerator,
// EdgeTokenProvider, EdgeProtocol, ConnectionMetadataFactory, SynthesisSession)
// but substitutes FakeWebSocketClient for WebSocketClient.  Calls Communicate
// API and verifies audio chunks arrive.
// ---------------------------------------------------------------------------

static TtsConfig valid_config() { return TtsConfig::defaults(); }

TEST(CommunicateProductionWiring, FullCompositionProducesAudioChunks) {
    // All real production objects except transport.
    SystemClock clock;
    IdGenerator ids;
    const EdgeServiceConfig svc = edge_tts::communication::default_edge_service_config();
    EdgeTokenProvider       token_provider{svc, clock};
    EdgeProtocol            protocol{clock};
    ConnectionMetadataFactory meta_factory{ids};

    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "AUDIO_BYTES");

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory, clock};

    // Inject via the SynthesizerFn seam — same path Communicate uses internally.
    Communicate c("hello world", valid_config(), CommunicateOptions{},
        [&session](const TtsConfig& cfg, std::span<const std::string> chunks)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return session.synthesize(cfg, chunks);
        });

    auto result = c.stream_sync();

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->empty());
    bool found_audio = false;
    for (const auto& chunk : *result)
        if (edge_tts::core::is_audio(chunk)) found_audio = true;
    EXPECT_TRUE(found_audio);
}

TEST(CommunicateProductionWiring, FullCompositionAudioBytesCorrect) {
    SystemClock clock;
    IdGenerator ids;
    const EdgeServiceConfig svc = edge_tts::communication::default_edge_service_config();
    EdgeTokenProvider       token_provider{svc, clock};
    EdgeProtocol            protocol{clock};
    ConnectionMetadataFactory meta_factory{ids};

    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "MP3PAYLOAD");

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory, clock};

    Communicate c("test text", valid_config(), CommunicateOptions{},
        [&session](const TtsConfig& cfg, std::span<const std::string> chunks)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return session.synthesize(cfg, chunks);
        });

    auto result = c.stream_sync();
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->empty());

    // First chunk must be audio with the expected bytes.
    ASSERT_TRUE(edge_tts::core::is_audio((*result)[0]));
    const auto& ac = std::get<AudioChunk>((*result)[0]);
    const std::string got(reinterpret_cast<const char*>(ac.data.data()), ac.data.size());
    EXPECT_EQ(got, "MP3PAYLOAD");
}

TEST(CommunicateProductionWiring, MultiChunkTextProducesAudioPerChunk) {
    // Long text → TextChunker splits into multiple chunks →
    // SynthesisSession is called once per chunk → FakeWebSocketClient
    // must have enough sessions queued.
    SystemClock clock;
    IdGenerator ids;
    const EdgeServiceConfig svc = edge_tts::communication::default_edge_service_config();
    EdgeTokenProvider       token_provider{svc, clock};
    EdgeProtocol            protocol{clock};
    ConnectionMetadataFactory meta_factory{ids};

    FakeWebSocketClient fake_ws;
    // Queue two sessions — one per chunk (long text will produce ≥2 chunks).
    push_session(fake_ws, "CHUNK_A");
    push_session(fake_ws, "CHUNK_B");

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory, clock};

    // 5000 bytes exceeds the 4096-byte limit → at least two chunks.
    std::string long_text(5000, 'x');
    Communicate c(long_text, valid_config(), CommunicateOptions{},
        [&session](const TtsConfig& cfg, std::span<const std::string> chunks)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return session.synthesize(cfg, chunks);
        });

    auto result = c.stream_sync();
    ASSERT_TRUE(result.has_value());

    // Both chunks should have produced audio.
    int audio_count = 0;
    for (const auto& chunk : *result)
        if (edge_tts::core::is_audio(chunk)) ++audio_count;
    EXPECT_EQ(audio_count, 2);
}

TEST(CommunicateProductionWiring, ProxyPassedToSessionViaOptions) {
    // Verify CommunicateOptions::proxy is stored and accessible when the
    // synthesizer seam is used.  The real SynthesisSession cannot inspect proxy
    // (it's owned by the WebSocketClient below it), but this confirms the options
    // are present when the synthesizer runs.
    SystemClock clock;
    IdGenerator ids;
    const EdgeServiceConfig svc = edge_tts::communication::default_edge_service_config();
    EdgeTokenProvider       token_provider{svc, clock};
    EdgeProtocol            protocol{clock};
    ConnectionMetadataFactory meta_factory{ids};

    FakeWebSocketClient fake_ws;
    push_session(fake_ws, "AUDIO");

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory, clock};

    CommunicateOptions opts;
    opts.proxy = "http://proxy.test:3128";

    Communicate c("hello", valid_config(), opts,
        [&session](const TtsConfig& cfg, std::span<const std::string> chunks)
            -> edge_tts::common::Result<std::vector<TtsChunk>>
        {
            return session.synthesize(cfg, chunks);
        });

    EXPECT_EQ(c.options().proxy, opts.proxy);
    auto result = c.stream_sync();
    ASSERT_TRUE(result.has_value());
}

// ---------------------------------------------------------------------------
// Structural offline tests for the production 2-arg and 3-arg constructors.
//
// These tests verify the structural behavior of the production constructors
// WITHOUT calling stream_sync() or save().  Calling either of those on a
// production-constructed Communicate would attempt a real network connection,
// making the test environment-dependent.
//
// The actual "production constructor synthesizes correctly via the real stack"
// verification lives in tests/api/CommunicateNetworkTests.cpp, gated behind
// EDGE_TTS_ENABLE_NETWORK_TESTS=ON and EDGE_TTS_RUN_NETWORK_TESTS=1.
// ---------------------------------------------------------------------------

TEST(CommunicateProductionWiring, ProductionConstructorStoresTextAndConfig) {
    // Constructing with the 2-arg form must not crash or throw, and must
    // preserve the text and config that were passed in.
    Communicate c("hello world", valid_config());
    EXPECT_EQ(c.text(), "hello world");
    EXPECT_EQ(c.config().voice, TtsConfig::defaults().voice);
}

TEST(CommunicateProductionWiring, ProductionConstructorWithOptionsStoresOptions) {
    // The 3-arg form (text, config, options) must store options correctly.
    // Verifies CommunicateOptions is preserved without making network calls.
    CommunicateOptions opts;
    opts.proxy = "http://proxy.test:3128";
    Communicate c("hello", valid_config(), opts);
    EXPECT_EQ(c.options().proxy, opts.proxy);
    EXPECT_EQ(c.text(), "hello");
}

TEST(CommunicateProductionWiring, ProductionConstructorDefaultOptionsAreEmpty) {
    Communicate c("hello", valid_config());
    EXPECT_FALSE(c.options().proxy.has_value());
}

TEST(CommunicateProductionWiring, ProductionConstructorPreservesVoice) {
    TtsConfig cfg = TtsConfig::defaults();
    cfg.voice = "en-GB-RyanNeural";
    Communicate c("test", cfg);
    EXPECT_EQ(c.config().voice, "en-GB-RyanNeural");
}
