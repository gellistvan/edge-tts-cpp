#pragma once
// Shared fixtures for the edge_tts_api_tests binary.  All helpers are in
// namespace edge_tts::test so they sit alongside the WebSocketFrameHelpers.
#include "api/SpeechSynthesizer.hpp"
#include "common/Clock.hpp"
#include "common/IdGenerator.hpp"
#include "common/Result.hpp"
#include "communication/ConnectionMetadata.hpp"
#include "communication/EdgeProtocol.hpp"
#include "communication/EdgeServiceConfig.hpp"
#include "communication/EdgeTokenProvider.hpp"
#include "communication/FakeWebSocketClient.hpp"
#include "communication/SynthesisSession.hpp"
#include "core/Chunk.hpp"
#include "core/TtsConfig.hpp"
#include "support/WebSocketFrameHelpers.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace edge_tts::test {

// Default valid TtsConfig.
inline core::TtsConfig valid_config() {
    return core::TtsConfig::defaults();
}

// Queue a minimal successful session: one audio frame + turn.end.
inline void push_session(communication::FakeWebSocketClient& ws,
                         std::string_view audio = "AUDIO") {
    ws.push_incoming(make_audio_frame(std::string{audio}));
    ws.push_incoming(make_turn_end());
}

// Wrap a SynthesisSession as a SynthesizerFn injection.  The session must
// outlive the returned fn.
inline api::SynthesizerFn make_seam(communication::SynthesisSession& session) {
    return [&session](const core::TtsConfig& cfg,
                      std::span<const std::string> chunks)
               -> common::Result<std::vector<core::TtsChunk>> {
        return session.synthesize(cfg, chunks);
    };
}

// Return a SynthesizerFn that always succeeds with the given chunks.
inline api::SynthesizerFn make_fake(std::vector<core::TtsChunk> chunks = {}) {
    return [chunks = std::move(chunks)](
               const core::TtsConfig&,
               std::span<const std::string>)
               -> common::Result<std::vector<core::TtsChunk>> {
        return common::Result<std::vector<core::TtsChunk>>::ok(chunks);
    };
}

// RAII guard: removes a file (if it exists) when the guard goes out of scope.
struct FileGuard {
    std::filesystem::path path;
    ~FileGuard() { std::filesystem::remove(path); }
};

// Read the entire contents of a file as text.
inline std::string read_file(const std::filesystem::path& p) {
    std::ifstream f(p);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>{}};
}

// Read the entire contents of a file as raw bytes.
inline std::vector<std::byte> read_file_binary(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    const std::vector<char> buf{std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>{}};
    std::vector<std::byte> out(buf.size());
    for (std::size_t i = 0; i < buf.size(); ++i)
        out[i] = static_cast<std::byte>(buf[i]);
    return out;
}

// All real production objects except the WebSocket transport, in initialization
// order (referenced objects declared before referencing objects).
struct TestWire {
    common::SystemClock                       clock;
    common::IdGenerator                       ids;
    communication::EdgeServiceConfig          svc{communication::default_edge_service_config()};
    communication::EdgeTokenProvider          tokens{svc, clock};
    communication::EdgeProtocol               protocol{clock};
    communication::ConnectionMetadataFactory  meta{ids};
};

} // namespace edge_tts::test
