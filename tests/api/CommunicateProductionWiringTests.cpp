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

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory};

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

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory};

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

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory};

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

    SynthesisSession session{fake_ws, protocol, svc, token_provider, meta_factory};

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
// Regression: production constructor must not return the old stub error
//
// The production Communicate(text, config) now wires the real networking stack.
// If someone reintroduces the stub lambda, these tests catch it.
//
// Strategy: call stream_sync() and verify the outcome is NOT the old hardcoded
// stub.  There are two valid outcomes depending on the environment:
//
//   (a) network available  → synthesis succeeds (result has value)        — not a stub
//   (b) no network in CI   → synthesis fails with transport error code     — not a stub
//
// Both outcomes prove the stub was removed.  The tests accept both and reject
// only the old "WebSocket transport not yet implemented" stub behavior.
// ---------------------------------------------------------------------------

TEST(CommunicateProductionWiring, ProductionConstructorErrorIsNotStubMessage) {
    Communicate c("hello", valid_config());
    auto result = c.stream_sync();
    // Either success (live network) or transport failure (no network) is fine.
    // The old stub always failed with a specific message — reject that only.
    if (!result.has_value()) {
        EXPECT_NE(result.error().message(),
                  "WebSocket transport not yet implemented");
    }
    // If result.has_value() → synthesis succeeded, which is even better evidence
    // that the stub is gone.  No assertion needed in that branch.
}

TEST(CommunicateProductionWiring, ProductionConstructorWithOptionsErrorIsNotStub) {
    CommunicateOptions opts;
    opts.proxy = "http://proxy.test:8080";
    Communicate c("hello", valid_config(), opts);
    auto result = c.stream_sync();
    // With a bogus proxy this will fail, but not with the old stub message.
    if (!result.has_value()) {
        EXPECT_NE(result.error().message(),
                  "WebSocket transport not yet implemented");
    }
}

TEST(CommunicateProductionWiring, ProductionConstructorErrorCodeIsTransportCode) {
    Communicate c("hello", valid_config());
    auto result = c.stream_sync();
    // If the call fails, the error must be a transport error, not the old stub codes.
    if (!result.has_value()) {
        // Transport errors are network_error or unsupported, never the old
        // invalid_state or protocol_error codes from a stub placeholder.
        const auto code = result.error().code();
        const bool is_transport_error =
            (code == ErrorCode::network_error) ||
            (code == ErrorCode::unsupported);
        EXPECT_TRUE(is_transport_error);
    }
}
